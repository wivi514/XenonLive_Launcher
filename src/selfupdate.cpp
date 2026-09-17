#include "selfupdate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "catalog.h"

namespace launcher {

namespace {

// Everything an install unpacks that is not the release itself.
bool IsStagingNoise(const std::filesystem::path& p) {
    const std::string name = p.filename().string();
    return name == "assets" || name.empty();
}

#ifndef _WIN32
// Starts `exe` detached — its own session, stdio to /dev/null — a moment
// from now, so it comes up after this process has gone and never as our
// child. Two forks, the classic way.
bool SpawnDetached(const std::string& exe, std::string& error) {
    const pid_t pid = fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        setsid();
        const pid_t grandchild = fork();
        if (grandchild != 0) _exit(0);
        // A short wait lets the old launcher close its window and release
        // its files (an AppImage's mount goes away with its runtime).
        sleep(1);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        execl(exe.c_str(), exe.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
}
#endif

}  // namespace

std::filesystem::path SelfPath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n));
#else
    if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) {
        return std::filesystem::path(appimage);
    }
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : exe;
#endif
}

std::filesystem::path SelfUpdateStagingDir() {
    return SelfPath().parent_path() / ".xenonlive-update";
}

bool SelfUpdatePossible(std::string& why) {
    if (std::string(LauncherVersion()) == "dev") {
        why = "this is a development build; it has no version to update from";
        return false;
    }
    const std::filesystem::path self = SelfPath();
    if (self.empty()) {
        why = "cannot tell where this launcher is";
        return false;
    }
    // The directory must take a new file: that is what a rename is.
    const std::filesystem::path probe = self.parent_path() / ".xenonlive-update-probe";
    std::ofstream out(probe, std::ios::binary);
    if (!out) {
        why = self.parent_path().string() + " is not writable; update it by hand";
        return false;
    }
    out.close();
    std::error_code ec;
    std::filesystem::remove(probe, ec);
    return true;
}

bool ApplySelfUpdate(const std::filesystem::path& staging, std::string& error) {
    std::error_code ec;
    const std::filesystem::path self = SelfPath();
    const std::filesystem::path dir = self.parent_path();
    if (self.empty()) {
        error = "cannot tell where this launcher is";
        return false;
    }

    const auto cleanup = [&] { std::filesystem::remove_all(staging, ec); };

    switch (PlatformFlavour()) {
    case Flavour::AppImage: {
        // One file. The old one is unlinked while still mounted — fine on
        // Linux, the mount holds the inode — and the new one takes its name.
        const std::filesystem::path fresh = staging / PlatformAssetName(LauncherSelf());
        if (!std::filesystem::is_regular_file(fresh, ec)) {
            error = "the update has no " + fresh.filename().string();
            return false;
        }
        const std::filesystem::path old = self.string() + ".old";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(self, old, ec);
        if (ec) {
            error = "cannot move the old launcher aside: " + ec.message();
            return false;
        }
        std::filesystem::rename(fresh, self, ec);
        if (ec) {
            std::filesystem::rename(old, self, ec);
            error = "cannot put the new launcher in place: " + ec.message();
            return false;
        }
#ifndef _WIN32
        ::chmod(self.string().c_str(), 0755);
#endif
        std::filesystem::remove(old, ec);
        cleanup();
#ifndef _WIN32
        return SpawnDetached(self.string(), error);
#else
        return true;
#endif
    }
    case Flavour::SteamDeck:
    case Flavour::Tar: {
        // A directory. Every staged file is renamed over its old self —
        // the running binary included; a rename swaps the inode under a
        // running program where a write would be refused.
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(staging, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const auto rel = std::filesystem::relative(entry.path(), staging, ec);
            if (rel.empty() || IsStagingNoise(*rel.begin())) continue;
            files.push_back(rel);
        }
        if (files.empty()) {
            error = "the update is empty";
            return false;
        }
        bool has_binary = false;
        for (const auto& rel : files) has_binary |= rel == self.filename();
        if (!has_binary) {
            error = "the update has no " + self.filename().string();
            return false;
        }
        for (const auto& rel : files) {
            const std::filesystem::path to = dir / rel;
            std::filesystem::create_directories(to.parent_path(), ec);
            std::filesystem::rename(staging / rel, to, ec);
            if (ec) {
                error = "cannot replace " + rel.string() + ": " + ec.message();
                return false;
            }
        }
#ifndef _WIN32
        ::chmod(self.string().c_str(), 0755);
#endif
        cleanup();
#ifndef _WIN32
        return SpawnDetached(self.string(), error);
#else
        return true;
#endif
    }
    case Flavour::Zip: {
#ifdef _WIN32
        // The .exe is locked while it runs. A script outlives us: waits for
        // this process to go, moves the staged files over, starts the new
        // launcher, deletes itself.
        const std::filesystem::path fresh = staging / (std::string(LauncherSelf().runtime) + ".exe");
        if (!std::filesystem::is_regular_file(fresh, ec)) {
            error = "the update has no " + fresh.filename().string();
            return false;
        }
        std::filesystem::remove_all(staging / "assets", ec);
        const std::filesystem::path script = dir / "xenonlive-update.cmd";
        {
            std::ofstream out(script, std::ios::binary);
            if (!out) {
                error = "cannot write " + script.string();
                return false;
            }
            // Waiting on the .exe itself, not on a PID: a rename of a running
            // executable is refused until it exits, so "ren" succeeding IS
            // the process being gone — no tasklist parsing, which matched a
            // PID against the memory column and waited forever. xcopy
            // rather than robocopy: every Windows has it and its exit code
            // is plain. ping is the sleep; timeout refuses to run without
            // a console, and this script has none.
            const std::string exe = self.filename().string();
            out << "@echo off\r\n"
                   "setlocal\r\n"
                   "set \"DIR=" << dir.string() << "\"\r\n"
                   "set \"SRC=" << staging.string() << "\"\r\n"
                   "set \"EXE=" << exe << "\"\r\n"
                   "set /a tries=0\r\n"
                   ":wait\r\n"
                   "ren \"%DIR%\\%EXE%\" \"%EXE%.old\" >nul 2>&1\r\n"
                   "if not errorlevel 1 goto swap\r\n"
                   "set /a tries+=1\r\n"
                   "if %tries% GEQ 120 goto fail\r\n"
                   "ping -n 2 127.0.0.1 >nul\r\n"
                   "goto wait\r\n"
                   ":swap\r\n"
                   "xcopy \"%SRC%\" \"%DIR%\\\" /E /Y /I /Q /H >nul 2>&1\r\n"
                   "if errorlevel 1 goto restore\r\n"
                   "rmdir /s /q \"%SRC%\" >nul 2>&1\r\n"
                   "del \"%DIR%\\%EXE%.old\" >nul 2>&1\r\n"
                   "start \"\" \"%DIR%\\%EXE%\"\r\n"
                   "del \"%~f0\"\r\n"
                   "exit /b 0\r\n"
                   ":restore\r\n"
                   "del \"%DIR%\\%EXE%\" >nul 2>&1\r\n"
                   "ren \"%DIR%\\%EXE%.old\" \"%EXE%\" >nul 2>&1\r\n"
                   ":fail\r\n"
                   "echo The update could not be applied (after %tries% waits). The old launcher was kept. > \"%DIR%\\xenonlive-update-failed.txt\"\r\n"
                   "start \"\" \"%DIR%\\%EXE%\"\r\n"
                   "del \"%~f0\"\r\n"
                   "exit /b 1\r\n";
        }
        std::wstring command = L"cmd.exe /c \"" + script.wstring() + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
            error = "cannot start the update script: " + std::to_string(GetLastError());
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
#else
        error = "a zip update on this platform is not a thing";
        return false;
#endif
    }
    }
    error = "unknown flavour";
    return false;
}

}  // namespace launcher
