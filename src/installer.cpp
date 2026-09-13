#include "installer.h"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <fstream>

#include "json.h"
#include "paths.h"
#include "sha256.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "archive.h"

namespace launcher {

namespace {

constexpr const char* kUserAgent = "XenonLive-Launcher/1.0 (+https://github.com/wivi514/XenonLive)";

size_t WriteToString(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t WriteToFile(char* data, size_t size, size_t count, void* user) {
    return std::fwrite(data, 1, size * count, static_cast<std::FILE*>(user));
}

// The same trust settings libxlive uses: the CA bundle the launcher found
// (or the player named), and no verification only when the player asked.
void ApplyTrust(CURL* curl) {
    if (const std::string ca = xlive::Env("XLIVE_CA_FILE"); !ca.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
    }
    if (xlive::Env("XLIVE_ALLOW_INSECURE") == "1") {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
}

struct ProgressContext {
    Installer* installer;
    std::atomic<bool>* cancel;
    std::mutex* mutex;
    InstallProgress* progress;
};

int OnProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressContext*>(user);
    if (ctx->cancel->load()) return 1;  // aborts the transfer
    std::lock_guard<std::mutex> lock(*ctx->mutex);
    ctx->progress->done = uint64_t(now);
    if (total > 0) ctx->progress->total = uint64_t(total);
    return 0;
}

// The one line in SHA256SUMS for `name`, or empty.
std::string SumFor(const std::string& sums, const std::string& name) {
    size_t pos = 0;
    while (pos < sums.size()) {
        size_t end = sums.find('\n', pos);
        if (end == std::string::npos) end = sums.size();
        const std::string line = sums.substr(pos, end - pos);
        pos = end + 1;
        // "<hex>  <name>" — two spaces, or one and an asterisk for binary mode.
        const size_t space = line.find(' ');
        if (space == std::string::npos || space != 64) continue;
        size_t start = space;
        while (start < line.size() && (line[start] == ' ' || line[start] == '*')) ++start;
        if (line.substr(start) == name) return line.substr(0, 64);
    }
    return {};
}

}  // namespace

const char* PhaseName(InstallPhase phase) {
    switch (phase) {
        case InstallPhase::Idle:        return "idle";
        case InstallPhase::Checking:    return "checking for the latest release";
        case InstallPhase::Downloading: return "downloading";
        case InstallPhase::Verifying:   return "verifying";
        case InstallPhase::Installing:  return "installing";
        case InstallPhase::Done:        return "done";
        case InstallPhase::Failed:      return "failed";
    }
    return "?";
}

void Installer::GlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void Installer::GlobalCleanup() { curl_global_cleanup(); }

Installer::~Installer() {
    cancel_.store(true);
    if (thread_.joinable()) thread_.join();
}

InstallProgress Installer::Poll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void Installer::Acknowledge() {
    if (busy_.load()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (progress_.phase == InstallPhase::Done || progress_.phase == InstallPhase::Failed) {
        progress_ = InstallProgress{};
    }
}

void Installer::Set(InstallPhase phase, std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_.phase = phase;
    progress_.message = std::move(message);
    progress_.done = 0;
    progress_.total = 0;
}

void Installer::Fail(std::string message) {
    std::fprintf(stderr, "[installer] %s\n", message.c_str());
    Set(InstallPhase::Failed, std::move(message));
}

void Installer::Cancel() { cancel_.store(true); }

bool Installer::CheckLatest(const CatalogGame& game) {
    if (busy_.exchange(true)) return false;
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = InstallProgress{};
        progress_.key = game.key;
        progress_.phase = InstallPhase::Checking;
    }
    thread_ = std::thread([this, &game] { Run(&game, {}, false); });
    return true;
}

bool Installer::Install(const CatalogGame& game, const std::filesystem::path& dir) {
    if (busy_.exchange(true)) return false;
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = InstallProgress{};
        progress_.key = game.key;
        progress_.phase = InstallPhase::Checking;
    }
    thread_ = std::thread([this, &game, dir] { Run(&game, dir, true); });
    return true;
}

// -- HTTP ---------------------------------------------------------------------

bool Installer::DownloadToString(const std::string& url, std::string& out, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }
    out.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    ApplyTrust(curl);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        error = curl_easy_strerror(code);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "HTTP " + std::to_string(status) + " from " + url;
        if (status == 403) error += " (GitHub's unauthenticated rate limit is 60 requests an hour)";
        return false;
    }
    return true;
}

bool Installer::DownloadToFile(const std::string& url, const std::filesystem::path& path,
                               uint64_t expected_size, std::string& error) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) {
        error = "cannot write " + path.string();
        return false;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        error = "curl_easy_init failed";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.total = expected_size;
    }
    ProgressContext ctx{this, &cancel_, &mutex_, &progress_};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    // No overall timeout on a 30 MB download; a stall is what ends it.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    ApplyTrust(curl);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    std::fclose(file);
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        error = "cancelled";
        return false;
    }
    if (code != CURLE_OK) {
        error = curl_easy_strerror(code);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "HTTP " + std::to_string(status) + " from " + url;
        return false;
    }
    return true;
}

bool Installer::FetchLatest(const CatalogGame& game, ReleaseInfo& out, std::string& error) {
    std::string body;
    const std::string url = std::string("https://api.github.com/repos/") + game.repo + "/releases/latest";
    if (!DownloadToString(url, body, error)) return false;
    auto parsed = xlive::json::Parse(body);
    if (!parsed.ok || !parsed.value.is_object()) {
        error = "GitHub answered with something that is not a release";
        return false;
    }
    out = ReleaseInfo{};
    out.tag = parsed.value["tag_name"].AsString();
    out.html_url = parsed.value["html_url"].AsString();
    // The exact name first; failing that, the same bundle, platform and
    // extension with something in between — a release has shipped
    // "CaseZeroRecomp-linux-x86_64-pm4fix.tar.zst", and a suffix like that
    // must not read as "no Linux build".
    const std::string wanted = PlatformAssetName(game);
    const size_t dot = wanted.find('.', wanted.find("x86_64"));
    const std::string stem = dot == std::string::npos ? wanted : wanted.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : wanted.substr(dot);
    const xlive::json::Value& assets = parsed.value["assets"];
    bool exact = false;
    for (size_t i = 0; i < assets.size(); ++i) {
        const std::string name = assets[i]["name"].AsString();
        if (name == "SHA256SUMS") {
            out.sums_url = assets[i]["browser_download_url"].AsString();
            continue;
        }
        const bool is_exact = name == wanted;
        const bool is_variant = !exact && name.size() > stem.size() + ext.size() &&
                                name.rfind(stem, 0) == 0 &&
                                name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
        if (is_exact || (is_variant && out.asset_url.empty())) {
            out.asset_name = name;
            out.asset_url = assets[i]["browser_download_url"].AsString();
            out.asset_size = uint64_t(assets[i]["size"].AsInt(0));
            exact = is_exact;
        }
    }
    if (out.tag.empty()) {
        error = "the release has no tag";
        return false;
    }
    if (out.asset_url.empty()) {
        error = out.tag + " has no " + wanted + " to download";
        return false;
    }
    return true;
}

// -- the job --------------------------------------------------------------------

void Installer::Run(const CatalogGame* game, std::filesystem::path dir, bool install) {
    std::string error;
    ReleaseInfo release;
    if (!FetchLatest(*game, release, error)) {
        Fail(error);
        busy_.store(false);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.release = release;
    }
    if (!install) {
        Set(InstallPhase::Done, release.tag);
        busy_.store(false);
        return;
    }

    // The checksums first, so a download that cannot be verified is not
    // started: a release without SHA256SUMS is refused, not trusted.
    std::string sums;
    if (release.sums_url.empty() || !DownloadToString(release.sums_url, sums, error)) {
        Fail(release.sums_url.empty() ? release.tag + " ships no SHA256SUMS; refusing to install it"
                                      : "SHA256SUMS: " + error);
        busy_.store(false);
        return;
    }
    const std::string expected = SumFor(sums, release.asset_name);
    if (expected.empty()) {
        Fail("SHA256SUMS does not list " + release.asset_name);
        busy_.store(false);
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        Fail("cannot create " + dir.string() + ": " + ec.message());
        busy_.store(false);
        return;
    }
    const std::filesystem::path part = dir / (release.asset_name + ".part");

    Set(InstallPhase::Downloading, release.asset_name);
    if (!DownloadToFile(release.asset_url, part, release.asset_size, error)) {
        std::filesystem::remove(part, ec);
        Fail(error);
        busy_.store(false);
        return;
    }

    Set(InstallPhase::Verifying, release.asset_name);
    const std::string actual = Sha256File(part.string());
    if (actual != expected) {
        std::filesystem::remove(part, ec);
        Fail("checksum mismatch on " + release.asset_name + ": the download is not what " +
             release.tag + " published");
        busy_.store(false);
        return;
    }

    Set(InstallPhase::Installing, release.asset_name);
    const auto ends_with = [&](const char* suffix) {
        const std::string s(suffix);
        return release.asset_name.size() >= s.size() &&
               release.asset_name.compare(release.asset_name.size() - s.size(), s.size(), s) == 0;
    };
    if (ends_with(".zip") || ends_with(".tar.zst") || ends_with(".tar.gz")) {
        // The archive carries one top directory (the bundle name); its
        // contents go straight into dir/, over whatever an earlier version
        // put there. The player's assets/ is never removed: an update is
        // files replaced, not a directory wiped.
        const auto on_progress = [this](uint64_t done, uint64_t total) {
            std::lock_guard<std::mutex> lock(mutex_);
            progress_.done = done;
            progress_.total = total;
        };
        const std::string top = std::string(game->bundle) + "/";
        const bool ok = ends_with(".zip")      ? ExtractZipInto(part, dir, top, error, on_progress)
                        : ends_with(".tar.gz") ? ExtractTarGzInto(part, dir, top, error, on_progress)
                                               : ExtractTarZstInto(part, dir, top, error, on_progress);
        std::filesystem::remove(part, ec);
        if (!ok) {
            Fail(error);
            busy_.store(false);
            return;
        }
    } else {
        // One file. Make it executable, then rename over the previous
        // version — an atomic replace, so a launch during an update sees
        // the old file or the new one and never a half-written one.
        const std::filesystem::path target = dir / release.asset_name;
#ifndef _WIN32
        ::chmod(part.string().c_str(), 0755);
#endif
        std::filesystem::rename(part, target, ec);
        if (ec) {
            std::filesystem::remove(part, ec);
            Fail("cannot put " + target.string() + " in place: " + ec.message());
            busy_.store(false);
            return;
        }
    }
    // Where the player's package goes. The AppImage's AppRun seeds it on the
    // first launch too, but the player is shown this path before then.
    std::filesystem::create_directories(dir / "assets" / "package", ec);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.installed = true;
    }
    Set(InstallPhase::Done, release.tag);
    busy_.store(false);
}

}  // namespace launcher
