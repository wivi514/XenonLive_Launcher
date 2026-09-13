#include "archive.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "miniz.h"

#include <algorithm>
#include "zstd.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace launcher {

bool ExtractZipInto(const std::filesystem::path& zip, const std::filesystem::path& dir,
                    const std::string& strip_top, std::string& error,
                    const std::function<void(uint64_t, uint64_t)>& progress) {
    mz_zip_archive archive;
    std::memset(&archive, 0, sizeof(archive));
    if (!mz_zip_reader_init_file(&archive, zip.string().c_str(), 0)) {
        error = "cannot open " + zip.filename().string() + " as a zip";
        return false;
    }
    std::error_code ec;
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) continue;
        std::string name = stat.m_filename;
        // The Windows bundle is zipped with backslashes in its entry names.
        // One separator, so the top directory strips and the paths below
        // are the same on every platform.
        for (char& c : name) if (c == '\\') c = '/';
        if (!strip_top.empty() && name.rfind(strip_top, 0) == 0) name = name.substr(strip_top.size());
        if (name.empty()) continue;
        // A zip is untrusted input even when it came from our own release
        // page: an entry that climbs out of the directory is refused.
        if (name.find("..") != std::string::npos || name[0] == '/' ||
            (name.size() > 1 && name[1] == ':')) {
            mz_zip_reader_end(&archive);
            error = "refusing zip entry " + name;
            return false;
        }
        const std::filesystem::path target = dir / name;
        if (mz_zip_reader_is_file_a_directory(&archive, i) || name.back() == '/') {
            std::filesystem::create_directories(target, ec);
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&archive, i, target.string().c_str(), 0)) {
            mz_zip_reader_end(&archive);
            error = "cannot extract " + name + " to " + dir.string();
            return false;
        }
        if (progress) progress(i + 1, count);
    }
    mz_zip_reader_end(&archive);
    return true;
}

}  // namespace launcher

// -- tar.zst --------------------------------------------------------------------

namespace launcher {

namespace {

// A ustar header, as bytes. Only the fields this reader uses are named.
struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};
static_assert(sizeof(TarHeader) == 512, "a tar header is one block");

uint64_t Octal(const char* field, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size && field[i]; ++i) {
        if (field[i] < '0' || field[i] > '7') continue;
        value = (value << 3) | uint64_t(field[i] - '0');
    }
    return value;
}

std::string Field(const char* field, size_t size) {
    return std::string(field, strnlen(field, size));
}

// Consumes the tar stream block by block as the decompressor produces it.
class TarWriter {
public:
    TarWriter(const std::filesystem::path& dir, std::string strip_top)
        : dir_(dir), strip_(std::move(strip_top)) {}

    bool Feed(const uint8_t* data, size_t size, std::string& error) {
        pending_.insert(pending_.end(), data, data + size);
        size_t offset = 0;
        while (true) {
            if (remaining_ > 0) {
                // The body of the current file.
                const size_t take = size_t(std::min<uint64_t>(remaining_, pending_.size() - offset));
                if (take == 0) break;
                if (capture_meta_) {
                    // A metadata body (a long name, a pax record): kept, up
                    // to its real size — the rest is block padding.
                    const size_t want = body_size_ > meta_.size() ? size_t(body_size_ - meta_.size()) : 0;
                    meta_.insert(meta_.end(), pending_.data() + offset,
                                 pending_.data() + offset + std::min(take, want));
                } else if (out_) {
                    const size_t real = body_size_ > written_ ? size_t(std::min<uint64_t>(take, body_size_ - written_)) : 0;
                    if (real && std::fwrite(pending_.data() + offset, 1, real, out_) != real) {
                        error = "cannot write " + current_.string();
                        return false;
                    }
                    written_ += real;
                }
                offset += take;
                remaining_ -= take;
                if (remaining_ == 0) FinishEntry();
                continue;
            }
            if (pending_.size() - offset < 512) break;
            const TarHeader* h = reinterpret_cast<const TarHeader*>(pending_.data() + offset);
            offset += 512;
            // Two zero blocks end the archive; one is tolerated as padding.
            bool zero = true;
            for (size_t i = 0; i < 512 && zero; ++i) zero = reinterpret_cast<const uint8_t*>(h)[i] == 0;
            if (zero) continue;
            if (!BeginEntry(*h, error)) return false;
        }
        pending_.erase(pending_.begin(), pending_.begin() + long(offset));
        return true;
    }

    bool Finish(std::string& error) {
        if (remaining_ > 0) {
            error = "archive ended inside " + current_.string();
            FinishEntry();
            return false;
        }
        return true;
    }

private:
    bool BeginEntry(const TarHeader& h, std::string& error) {
        const uint64_t size = Octal(h.size, sizeof(h.size));
        // Bodies are padded to 512.
        padded_ = (size + 511) & ~uint64_t(511);
        std::string name = Field(h.name, sizeof(h.name));
        if (std::memcmp(h.magic, "ustar", 5) == 0) {
            const std::string prefix = Field(h.prefix, sizeof(h.prefix));
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        if (!long_name_.empty()) {
            name = long_name_;
            long_name_.clear();
        }
        if (!pax_path_.empty()) {
            name = pax_path_;
            pax_path_.clear();
        }
        const char type = h.typeflag;

        // Metadata entries: the body is read into a buffer, not a file.
        if (type == 'L' || type == 'x' || type == 'g') {
            meta_type_ = type;
            meta_.clear();
            capture_meta_ = true;
            remaining_ = padded_;
            body_size_ = size;
            out_ = nullptr;
            return true;
        }
        capture_meta_ = false;

        if (!strip_.empty() && name.rfind(strip_, 0) == 0) name = name.substr(strip_.size());
        while (!name.empty() && name.front() == '/') name.erase(0, 1);
        if (name.empty() || name.find("..") != std::string::npos) {
            if (!name.empty()) {
                error = "refusing archive entry " + name;
                return false;
            }
            // The top directory itself.
            remaining_ = padded_;
            out_ = nullptr;
            return true;
        }
        current_ = dir_ / name;
        std::error_code ec;
        switch (type) {
            case '5':
                std::filesystem::create_directories(current_, ec);
                remaining_ = padded_;
                out_ = nullptr;
                break;
            case '2': {
                std::filesystem::create_directories(current_.parent_path(), ec);
                std::filesystem::remove(current_, ec);
                std::filesystem::create_symlink(Field(h.linkname, sizeof(h.linkname)), current_, ec);
                if (ec) {
                    error = "cannot create link " + current_.string() + ": " + ec.message();
                    return false;
                }
                remaining_ = padded_;
                out_ = nullptr;
                break;
            }
            case '0':
            case '\0':
            case '7': {
                std::filesystem::create_directories(current_.parent_path(), ec);
                std::filesystem::remove(current_, ec);  // a symlink here must not be followed
                out_ = std::fopen(current_.string().c_str(), "wb");
                if (!out_) {
                    error = "cannot write " + current_.string();
                    return false;
                }
                mode_ = uint32_t(Octal(h.mode, sizeof(h.mode)));
                remaining_ = padded_;
                body_size_ = size;
                written_ = 0;
                if (size == 0) FinishEntry();
                break;
            }
            default:
                // Hard links, devices, FIFOs: nothing a game bundle carries.
                remaining_ = padded_;
                out_ = nullptr;
                break;
        }
        return true;
    }

    void FinishEntry() {
        if (capture_meta_) {
            capture_meta_ = false;
            if (meta_type_ == 'L') {
                long_name_ = std::string(meta_.begin(), meta_.end());
                while (!long_name_.empty() && long_name_.back() == '\0') long_name_.pop_back();
            } else if (meta_type_ == 'x') {
                // "<len> path=<value>\n" records.
                const std::string text(meta_.begin(), meta_.end());
                size_t pos = 0;
                while (pos < text.size()) {
                    const size_t space = text.find(' ', pos);
                    if (space == std::string::npos) break;
                    const size_t len = size_t(std::strtoul(text.c_str() + pos, nullptr, 10));
                    if (len == 0 || pos + len > text.size()) break;
                    const std::string record = text.substr(space + 1, pos + len - space - 2);
                    if (record.rfind("path=", 0) == 0) pax_path_ = record.substr(5);
                    pos += len;
                }
            }
            return;
        }
        if (out_) {
            std::fclose(out_);
            out_ = nullptr;
#ifndef _WIN32
            ::chmod(current_.string().c_str(), mode_ & 0777);
#endif
        }
    }

    std::filesystem::path dir_;
    std::string strip_;
    std::vector<uint8_t> pending_;
    uint64_t remaining_ = 0;  // padded bytes still to consume of the current body
    uint64_t padded_ = 0;
    uint64_t body_size_ = 0;
    std::FILE* out_ = nullptr;
    std::filesystem::path current_;
    uint32_t mode_ = 0644;
    bool capture_meta_ = false;
    char meta_type_ = 0;
    std::vector<uint8_t> meta_;
    std::string long_name_;
    std::string pax_path_;
    uint64_t written_ = 0;
};

}  // namespace

bool ExtractTarZstInto(const std::filesystem::path& archive, const std::filesystem::path& dir,
                       const std::string& strip_top, std::string& error,
                       const std::function<void(uint64_t, uint64_t)>& progress) {
    std::FILE* in = std::fopen(archive.string().c_str(), "rb");
    if (!in) {
        error = "cannot open " + archive.filename().string();
        return false;
    }
    std::error_code ec;
    const uint64_t total = std::filesystem::file_size(archive, ec);

    ZSTD_DStream* stream = ZSTD_createDStream();
    if (!stream) {
        std::fclose(in);
        error = "zstd: cannot create a decoder";
        return false;
    }
    ZSTD_initDStream(stream);
    std::vector<uint8_t> in_buf(ZSTD_DStreamInSize());
    std::vector<uint8_t> out_buf(ZSTD_DStreamOutSize());
    TarWriter writer(dir, strip_top);
    uint64_t consumed = 0;
    bool ok = true;

    while (ok) {
        const size_t n = std::fread(in_buf.data(), 1, in_buf.size(), in);
        if (n == 0) break;
        consumed += n;
        ZSTD_inBuffer input{in_buf.data(), n, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output{out_buf.data(), out_buf.size(), 0};
            const size_t rc = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(rc)) {
                error = std::string("zstd: ") + ZSTD_getErrorName(rc);
                ok = false;
                break;
            }
            if (!writer.Feed(out_buf.data(), output.pos, error)) {
                ok = false;
                break;
            }
        }
        if (progress) progress(consumed, total);
    }
    ZSTD_freeDStream(stream);
    std::fclose(in);
    if (ok) ok = writer.Finish(error);
    return ok;
}

bool ExtractTarGzInto(const std::filesystem::path& archive, const std::filesystem::path& dir,
                      const std::string& strip_top, std::string& error,
                      const std::function<void(uint64_t, uint64_t)>& progress) {
    std::FILE* in = std::fopen(archive.string().c_str(), "rb");
    if (!in) {
        error = "cannot open " + archive.filename().string();
        return false;
    }
    std::error_code ec;
    const uint64_t total = std::filesystem::file_size(archive, ec);

    // A gzip member is a header, a raw deflate stream, and an 8-byte trailer
    // (CRC-32, size). miniz inflates raw deflate; the header is walked here.
    // Multiple members (a concatenated .gz) are handled by starting over
    // when one stream ends with bytes left.
    mz_stream stream{};
    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
        std::fclose(in);
        error = "gzip: cannot create a decoder";
        return false;
    }
    std::vector<uint8_t> in_buf(1 << 16);
    std::vector<uint8_t> out_buf(1 << 16);
    std::vector<uint8_t> pending;  // input not yet handed on
    TarWriter writer(dir, strip_top);
    uint64_t consumed = 0;
    bool ok = true;
    enum { Header, Deflate, Trailer } state = Header;
    size_t trailer_left = 0;

    // Walks a gzip member header at the front of buf. 1 with `used` set,
    // 0 when more bytes are needed, -1 when it is not gzip at all.
    const auto parse_header = [](const std::vector<uint8_t>& buf, size_t& used) -> int {
        if (buf.size() < 10) return 0;
        if (buf[0] != 0x1f || buf[1] != 0x8b || buf[2] != 8) return -1;
        const uint8_t flags = buf[3];
        size_t at = 10;
        if (flags & 0x04) {  // FEXTRA
            if (buf.size() < at + 2) return 0;
            at += 2 + (buf[at] | (size_t(buf[at + 1]) << 8));
            if (buf.size() < at) return 0;
        }
        for (int bit : {0x08, 0x10}) {  // FNAME, FCOMMENT: NUL-terminated
            if (!(flags & bit)) continue;
            while (at < buf.size() && buf[at] != 0) ++at;
            if (at >= buf.size()) return 0;
            ++at;
        }
        if (flags & 0x02) {  // FHCRC
            at += 2;
            if (buf.size() < at) return 0;
        }
        used = at;
        return 1;
    };

    while (ok) {
        const size_t n = std::fread(in_buf.data(), 1, in_buf.size(), in);
        if (n == 0) break;
        consumed += n;
        pending.insert(pending.end(), in_buf.begin(), in_buf.begin() + long(n));
        size_t at = 0;
        bool need_more = false;
        while (ok && !need_more && at < pending.size()) {
            switch (state) {
            case Header: {
                std::vector<uint8_t> rest(pending.begin() + long(at), pending.end());
                size_t used = 0;
                const int r = parse_header(rest, used);
                if (r < 0) {
                    error = "gzip: not a gzip file";
                    ok = false;
                } else if (r == 0) {
                    need_more = true;
                } else {
                    at += used;
                    mz_inflateReset(&stream);
                    state = Deflate;
                }
                break;
            }
            case Deflate: {
                stream.next_in = pending.data() + at;
                stream.avail_in = unsigned(pending.size() - at);
                stream.next_out = out_buf.data();
                stream.avail_out = unsigned(out_buf.size());
                const int rc = mz_inflate(&stream, MZ_NO_FLUSH);
                const size_t produced = out_buf.size() - stream.avail_out;
                at = pending.size() - stream.avail_in;
                if (produced && !writer.Feed(out_buf.data(), produced, error)) {
                    ok = false;
                } else if (rc == MZ_STREAM_END) {
                    state = Trailer;
                    trailer_left = 8;
                } else if (rc == MZ_BUF_ERROR && produced == 0) {
                    need_more = true;
                } else if (rc != MZ_OK && rc != MZ_BUF_ERROR) {
                    error = std::string("gzip: ") + (mz_error(rc) ? mz_error(rc) : "inflate failed");
                    ok = false;
                }
                break;
            }
            case Trailer: {
                // CRC-32 and size: eight bytes to step over, then maybe
                // another member.
                const size_t skip = std::min(trailer_left, pending.size() - at);
                at += skip;
                trailer_left -= skip;
                if (trailer_left == 0) state = Header;
                break;
            }
            }
        }
        if (ok) pending.erase(pending.begin(), pending.begin() + long(std::min(at, pending.size())));
        if (progress) progress(consumed, total);
    }
    mz_inflateEnd(&stream);
    std::fclose(in);
    if (ok) ok = writer.Finish(error);
    return ok;
}

}  // namespace launcher
