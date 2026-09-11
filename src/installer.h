// Installing and updating a catalog game from its GitHub release.
//
// One job at a time, on its own thread: ask GitHub for the latest release,
// download this platform's asset, check it against the release's
// SHA256SUMS, and put it in place. The UI thread never waits; it reads
// Poll() once a frame and draws a progress bar. Cancel() stops a download
// at the next progress callback.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "catalog.h"

namespace launcher {

struct ReleaseInfo {
    std::string tag;       // "v1.0.1"
    std::string html_url;  // the release page
    std::string asset_name;
    std::string asset_url;
    uint64_t asset_size = 0;
    std::string sums_url;  // SHA256SUMS, empty when the release has none
    bool valid() const { return !tag.empty(); }
};

enum class InstallPhase { Idle, Checking, Downloading, Verifying, Installing, Done, Failed };

struct InstallProgress {
    InstallPhase phase = InstallPhase::Idle;
    std::string key;  // which catalog game the job is about
    uint64_t done = 0;
    uint64_t total = 0;
    std::string message;  // the error, for Failed; a note otherwise
    ReleaseInfo release;  // known once Checking has passed
    // True for a Done that installed something (rather than only checked).
    bool installed = false;
};

const char* PhaseName(InstallPhase phase);

class Installer {
public:
    Installer() = default;
    ~Installer();
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    bool busy() const { return busy_.load(); }

    // Asks GitHub for the latest release and stops there. Done with
    // installed=false; the release is in the progress.
    bool CheckLatest(const CatalogGame& game);
    // Check, download, verify, install into `dir`. Done with installed=true.
    bool Install(const CatalogGame& game, const std::filesystem::path& dir);
    void Cancel();

    InstallProgress Poll() const;
    // Clears a Done or Failed so the UI can show the resting state again.
    void Acknowledge();

    // Once per process, before any job.
    static void GlobalInit();
    static void GlobalCleanup();

private:
    void Run(const CatalogGame* game, std::filesystem::path dir, bool install);
    bool FetchLatest(const CatalogGame& game, ReleaseInfo& out, std::string& error);
    bool DownloadToFile(const std::string& url, const std::filesystem::path& path,
                        uint64_t expected_size, std::string& error);
    bool DownloadToString(const std::string& url, std::string& out, std::string& error);
    void Set(InstallPhase phase, std::string message = {});
    void Fail(std::string message);

    std::thread thread_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
    InstallProgress progress_;
};

}  // namespace launcher
