// launcher.json: the server, and the titles the player has added.
//
// It lives in the XenonLive data directory beside session.json, so the one
// place a player configures the server is also the place the game finds it.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace launcher {

struct TitleEntry {
    uint32_t title_id = 0;
    std::string name;
    // The port's runtime executable, and the directory to run it in.
    std::string exe;
    std::string cwd;
    // Set in the child's environment on top of the launcher's own.
    std::map<std::string, std::string> env;
    // For a game the launcher installed from the catalog: which one, and the
    // release tag it holds. Empty for a build the player pointed at by hand
    // in launcher.json (a dev tree, say), which the launcher runs but does
    // not update.
    std::string key;
    std::string version;

    bool managed() const { return !key.empty(); }
};

struct Config {
    std::string server = "http://127.0.0.1:18080";
    // Sets XLIVE_ALLOW_INSECURE=1 for the launcher and every title it
    // starts: plain http and no certificate check. Local development only.
    bool allow_insecure = true;
    // An optional TTF for a font with accents or CJK; empty means ImGui's
    // built-in ProggyClean, which is ASCII only.
    std::string font_path;
    float font_size = 16.0f;
    // Where installed games go; empty means GamesDir()'s default.
    std::string games_dir;
    std::vector<TitleEntry> titles;

    const TitleEntry* FindTitle(uint32_t title_id) const;
    TitleEntry* FindTitle(uint32_t title_id);
    // The install directory for a catalog game: <games dir>/<key>.
    std::filesystem::path InstallDir(const std::string& key) const;
};

// The XenonLive data directory: XLIVE_DATA_DIR, else the platform default
// libxlive uses (~/.config/XenonLive on Linux).
std::filesystem::path DataDir();
std::filesystem::path ConfigPath();
// Where games are installed by default. Not the config directory — a game
// is a gigabyte of unpacked assets — but the platform's data directory:
// ~/.local/share/XenonLive/games, %LOCALAPPDATA%\XenonLive\games,
// ~/Library/Application Support/XenonLive/games. Under XLIVE_DATA_DIR it is
// <that>/games, so a test never installs into the real one.
std::filesystem::path DefaultGamesDir();

// A missing file is the default config, not an error. A malformed one is
// reported and also the default: the launcher must start regardless.
bool LoadConfig(Config& out, std::string& error);
bool SaveConfig(const Config& config, std::string& error);

// "58410b00" -> 0x58410B00. False when the text is not 1-8 hex digits.
bool ParseTitleId(const std::string& text, uint32_t& out);
std::string TitleIdHex(uint32_t title_id);

}  // namespace launcher
