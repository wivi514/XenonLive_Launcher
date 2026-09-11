// Starting a title, and knowing when it has stopped.
//
// The launcher stays open while the game runs — that is what keeps the player
// "online" between games — so this never waits on the child. Start() returns
// as soon as the process exists and Poll() is asked once a frame.
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace launcher {

class Process {
public:
    Process() = default;
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Spawns `exe` in `cwd` (empty: inherit) with the launcher's environment
    // plus `env` on top. False, with a reason, when the process could not be
    // created; a child that starts and then fails is a normal exit with a
    // code, which is not this call's to know.
    bool Start(const std::string& exe, const std::string& cwd,
               const std::map<std::string, std::string>& env, std::string& error);

    // Non-blocking. Notices an exit and records the code.
    void Poll();

    bool running() const { return running_; }
    // Meaningful once running() has gone false after a Start().
    int exit_code() const { return exit_code_; }
    bool exited_abnormally() const { return abnormal_; }
    int64_t pid() const { return pid_; }

private:
    bool running_ = false;
    int exit_code_ = 0;
    bool abnormal_ = false;
    int64_t pid_ = 0;
#ifdef _WIN32
    void* handle_ = nullptr;
#endif
};

}  // namespace launcher
