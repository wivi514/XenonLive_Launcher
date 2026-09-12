// What a port left under <data dir>/captures/: one directory per press of
// the capture key, a capture.json naming the files. The contract is
// XenonLive's docs/bug-reports.md. Nothing here touches the network — the
// launcher shows a capture, the player decides.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace launcher {

struct CaptureFile {
    std::string name;  // as in the directory and on the server
    std::string type;  // "image/png", "text/plain"
    std::string what;  // the port's one-line description, shown beside it
    std::filesystem::path path;
    std::uintmax_t size = 0;
    bool present = false;
};

struct Capture {
    std::string id;  // the directory's name
    std::filesystem::path dir;
    uint32_t title_id = 0;
    std::string game;
    std::string game_version;
    std::string captured_at;  // RFC 3339, as written
    std::string trigger;      // "F9"
    std::vector<CaptureFile> files;
    std::string system_json;  // the "system" object, re-serialised
    std::vector<std::pair<std::string, std::string>> system;  // for display
    // A directory that is not a capture (no capture.json, bad JSON, a file
    // it names missing): shown with the reason so it can be deleted.
    std::string problem;
};

// Every capture, newest first by name (the ports name them by time).
std::vector<Capture> ScanCaptures(const std::filesystem::path& captures_dir);
// Removes the directory and everything in it.
bool RemoveCapture(const Capture& capture, std::string& error);

}  // namespace launcher
