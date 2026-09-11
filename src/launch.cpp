#include "launch.h"

#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace launcher {

namespace {

// The launcher's own environment with `extra` laid over it. A key present in
// both is the title's, because that is what "set in the launcher for this
// title" means.
std::map<std::string, std::string> MergedEnv(const std::map<std::string, std::string>& extra) {
    std::map<std::string, std::string> merged;
#ifdef _WIN32
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (wchar_t* p = block; *p; p += wcslen(p) + 1) {
            const std::wstring entry(p);
            const size_t eq = entry.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            const auto narrow = [](const std::wstring& w) {
                std::string out;
                const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    out.resize(size_t(n - 1));
                    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
                }
                return out;
            };
            merged[narrow(entry.substr(0, eq))] = narrow(entry.substr(eq + 1));
        }
        FreeEnvironmentStringsW(block);
    }
#else
    for (char** p = environ; p && *p; ++p) {
        const char* eq = std::strchr(*p, '=');
        if (!eq) continue;
        merged[std::string(*p, size_t(eq - *p))] = eq + 1;
    }
#endif
    for (const auto& [key, value] : extra) merged[key] = value;
    return merged;
}

}  // namespace

Process::~Process() {
#ifdef _WIN32
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#endif
    // A running game is deliberately NOT killed when the launcher goes: the
    // player closing the launcher window is not the player quitting the game.
}

#ifdef _WIN32

bool Process::Start(const std::string& exe, const std::string& cwd,
                    const std::map<std::string, std::string>& env, std::string& error) {
    if (running_) {
        error = "already running";
        return false;
    }
    const auto widen = [](const std::string& s) {
        std::wstring out;
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n > 0) {
            out.resize(size_t(n - 1));
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
        }
        return out;
    };

    // CreateProcessW wants a mutable command line and an environment block of
    // NUL-separated "K=V" strings ending in a double NUL.
    std::wstring command = L"\"" + widen(exe) + L"\"";
    std::vector<wchar_t> block;
    for (const auto& [key, value] : MergedEnv(env)) {
        const std::wstring entry = widen(key) + L"=" + widen(value);
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring wcwd = widen(cwd);
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, block.data(),
                        cwd.empty() ? nullptr : wcwd.c_str(), &si, &pi)) {
        error = "CreateProcess failed: " + std::to_string(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = pi.hProcess;
    pid_ = int64_t(pi.dwProcessId);
    running_ = true;
    abnormal_ = false;
    exit_code_ = 0;
    return true;
}

void Process::Poll() {
    if (!running_ || !handle_) return;
    DWORD code = 0;
    if (WaitForSingleObject(static_cast<HANDLE>(handle_), 0) != WAIT_OBJECT_0) return;
    if (GetExitCodeProcess(static_cast<HANDLE>(handle_), &code)) exit_code_ = int(code);
    abnormal_ = code >= 0xC0000000u;  // an NTSTATUS: a crash, not a return
    running_ = false;
}

#else

bool Process::Start(const std::string& exe, const std::string& cwd,
                    const std::map<std::string, std::string>& env, std::string& error) {
    if (running_) {
        error = "already running";
        return false;
    }

    std::vector<std::string> entries;
    for (const auto& [key, value] : MergedEnv(env)) entries.push_back(key + "=" + value);
    std::vector<char*> envp;
    for (std::string& entry : entries) envp.push_back(entry.data());
    envp.push_back(nullptr);
    char* const argv[] = {const_cast<char*>(exe.c_str()), nullptr};

    // A pipe the child writes its exec failure to. It is close-on-exec, so a
    // successful exec closes it and the parent reads nothing; a failed one
    // sends errno back, and the parent can say why rather than reporting a
    // process that "exited immediately".
    int fds[2];
    if (pipe(fds) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        close(fds[0]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            const int err = errno;
            (void)!write(fds[1], &err, sizeof(err));
            _exit(127);
        }
        execve(exe.c_str(), argv, envp.data());
        const int err = errno;
        (void)!write(fds[1], &err, sizeof(err));
        _exit(127);
    }
    close(fds[1]);
    int err = 0;
    const ssize_t n = read(fds[0], &err, sizeof(err));
    close(fds[0]);
    if (n == sizeof(err)) {
        int status = 0;
        waitpid(pid, &status, 0);
        error = exe + ": " + std::strerror(err);
        return false;
    }

    pid_ = pid;
    running_ = true;
    abnormal_ = false;
    exit_code_ = 0;
    return true;
}

void Process::Poll() {
    if (!running_) return;
    int status = 0;
    const pid_t r = waitpid(pid_t(pid_), &status, WNOHANG);
    if (r == 0) return;
    if (r < 0) {
        // Reaped by someone else, or never ours. Either way it is gone.
        running_ = false;
        return;
    }
    if (WIFEXITED(status)) {
        exit_code_ = WEXITSTATUS(status);
        abnormal_ = false;
    } else if (WIFSIGNALED(status)) {
        exit_code_ = WTERMSIG(status);
        abnormal_ = true;
    }
    running_ = false;
}

#endif

}  // namespace launcher
