#include "archive.h"

#include <cstring>

#include "miniz.h"

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
