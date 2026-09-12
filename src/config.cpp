#include "config.h"

#include <cstdio>
#include <fstream>

#include "json.h"
#include "paths.h"

namespace launcher {

const TitleEntry* Config::FindTitle(uint32_t title_id) const {
    for (const TitleEntry& entry : titles) {
        if (entry.title_id == title_id) return &entry;
    }
    return nullptr;
}

TitleEntry* Config::FindTitle(uint32_t title_id) {
    for (TitleEntry& entry : titles) {
        if (entry.title_id == title_id) return &entry;
    }
    return nullptr;
}

std::filesystem::path Config::InstallDir(const std::string& key) const {
    return (games_dir.empty() ? DefaultGamesDir() : std::filesystem::path(games_dir)) / key;
}

std::filesystem::path DataDir() { return xlive::UserDataDir(); }
std::filesystem::path ConfigPath() { return DataDir() / "launcher.json"; }

std::filesystem::path DefaultGamesDir() {
    if (const std::string override_dir = xlive::Env("XLIVE_DATA_DIR"); !override_dir.empty()) {
        return std::filesystem::path(override_dir) / "games";
    }
#ifdef _WIN32
    if (const std::string local = xlive::Env("LOCALAPPDATA"); !local.empty()) {
        return std::filesystem::path(local) / "XenonLive" / "games";
    }
#elif defined(__APPLE__)
    if (const std::string home = xlive::Env("HOME"); !home.empty()) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "XenonLive" / "games";
    }
#else
    if (const std::string xdg = xlive::Env("XDG_DATA_HOME"); !xdg.empty()) {
        return std::filesystem::path(xdg) / "XenonLive" / "games";
    }
    if (const std::string home = xlive::Env("HOME"); !home.empty()) {
        return std::filesystem::path(home) / ".local" / "share" / "XenonLive" / "games";
    }
#endif
    return DataDir() / "games";
}

bool ParseTitleId(const std::string& text, uint32_t& out) {
    if (text.empty() || text.size() > 8) return false;
    uint32_t value = 0;
    for (char c : text) {
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') digit = uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = uint32_t(c - 'A' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

std::string TitleIdHex(uint32_t title_id) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", title_id);
    return buf;
}

bool LoadConfig(Config& out, std::string& error) {
    out = Config{};
    error.clear();

    std::ifstream in(ConfigPath(), std::ios::binary);
    if (!in) return true;  // no file: the defaults
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    auto parsed = xlive::json::Parse(text);
    if (!parsed.ok || !parsed.value.is_object()) {
        error = "launcher.json is not valid JSON: " + parsed.error;
        return false;
    }
    const xlive::json::Value& doc = parsed.value;
    if (doc.Has("server")) out.server = doc["server"].AsString();
    if (doc.Has("allow_insecure")) out.allow_insecure = doc["allow_insecure"].AsBool();
    if (doc.Has("font_path")) out.font_path = doc["font_path"].AsString();
    if (doc.Has("font_size")) out.font_size = float(doc["font_size"].AsDouble(18.0));
    if (doc.Has("games_dir")) out.games_dir = doc["games_dir"].AsString();

    const xlive::json::Value& titles = doc["titles"];
    for (size_t i = 0; i < titles.size(); ++i) {
        const xlive::json::Value& t = titles[i];
        TitleEntry entry;
        if (!ParseTitleId(t["title_id"].AsString(), entry.title_id) || entry.title_id == 0) {
            error = "launcher.json: titles[" + std::to_string(i) +
                    "] has no usable title_id; skipped";
            continue;
        }
        entry.name = t["name"].AsString();
        if (entry.name.empty()) entry.name = TitleIdHex(entry.title_id);
        entry.exe = t["exe"].AsString();
        entry.cwd = t["cwd"].AsString();
        entry.key = t["key"].AsString();
        entry.version = t["version"].AsString();
        const xlive::json::Value& env = t["env"];
        for (const std::string& key : env.Keys()) {
            entry.env[key] = env[key].AsString();
        }
        out.titles.push_back(std::move(entry));
    }
    return error.empty();
}

bool SaveConfig(const Config& config, std::string& error) {
    using xlive::json::Value;
    error.clear();

    Value doc = Value::Object();
    doc.Set("server", Value::String(config.server));
    doc.Set("allow_insecure", Value::Bool(config.allow_insecure));
    if (!config.font_path.empty()) doc.Set("font_path", Value::String(config.font_path));
    doc.Set("font_size", Value::Number(config.font_size));
    if (!config.games_dir.empty()) doc.Set("games_dir", Value::String(config.games_dir));

    Value titles = Value::Array();
    for (const TitleEntry& entry : config.titles) {
        Value t = Value::Object();
        t.Set("title_id", Value::String(TitleIdHex(entry.title_id)));
        t.Set("name", Value::String(entry.name));
        t.Set("exe", Value::String(entry.exe));
        t.Set("cwd", Value::String(entry.cwd));
        if (entry.managed()) {
            t.Set("key", Value::String(entry.key));
            t.Set("version", Value::String(entry.version));
        }
        Value env = Value::Object();
        for (const auto& [key, value] : entry.env) env.Set(key, Value::String(value));
        t.Set("env", std::move(env));
        titles.Push(std::move(t));
    }
    doc.Set("titles", std::move(titles));

    const std::filesystem::path path = ConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // A temp file and a rename, the same as the library's own writes: a
    // crash mid-write leaves the old config, not half of the new one.
    const std::string temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + temp;
            return false;
        }
        const std::string text = doc.Serialize();
        out.write(text.data(), std::streamsize(text.size()));
        out.flush();
        if (!out) {
            error = "cannot write " + temp;
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temp, path, ec);
    }
    if (ec) {
        error = "cannot replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

}  // namespace launcher
