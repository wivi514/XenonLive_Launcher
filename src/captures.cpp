#include "captures.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "json.h"

namespace launcher {

namespace {

bool ParseHex32(const std::string& text, uint32_t& out) {
    if (text.empty() || text.size() > 8) return false;
    uint32_t v = 0;
    for (char c : text) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | uint32_t(d);
    }
    out = v;
    return true;
}

bool SafeName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return name[0] != '.';
}

Capture ReadCapture(const std::filesystem::path& dir) {
    Capture capture;
    capture.id = dir.filename().string();
    capture.dir = dir;

    std::ifstream in(dir / "capture.json", std::ios::binary);
    if (!in) {
        capture.problem = "no capture.json";
        return capture;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = xlive::json::Parse(text);
    if (!parsed.ok) {
        capture.problem = "capture.json is not valid JSON: " + parsed.error;
        return capture;
    }
    const xlive::json::Value& doc = parsed.value;
    ParseHex32(doc["title_id"].AsString(), capture.title_id);
    capture.game = doc["game"].AsString();
    capture.game_version = doc["game_version"].AsString();
    capture.captured_at = doc["captured_at"].AsString();
    capture.trigger = doc["trigger"].AsString();

    const xlive::json::Value& files = doc["files"];
    for (size_t i = 0; i < files.size(); ++i) {
        CaptureFile f;
        f.name = files[i]["name"].AsString();
        f.type = files[i]["type"].AsString();
        f.what = files[i]["what"].AsString();
        if (!SafeName(f.name)) {
            capture.problem = "a file has an unusable name: " + f.name;
            continue;
        }
        f.path = dir / f.name;
        std::error_code ec;
        f.present = std::filesystem::is_regular_file(f.path, ec);
        if (f.present) f.size = std::filesystem::file_size(f.path, ec);
        if (!f.present && capture.problem.empty()) capture.problem = "missing " + f.name;
        capture.files.push_back(std::move(f));
    }
    if (capture.files.empty() && capture.problem.empty()) capture.problem = "names no files";

    const xlive::json::Value& system = doc["system"];
    if (system.is_object()) {
        capture.system_json = system.Serialize();
        for (const auto& key : system.Keys()) {
            const xlive::json::Value& v = system[key];
            std::string shown;
            if (v.is_string()) shown = v.AsString();
            else shown = v.Serialize();
            capture.system.emplace_back(key, shown);
        }
    }
    return capture;
}

}  // namespace

std::vector<Capture> ScanCaptures(const std::filesystem::path& captures_dir) {
    std::vector<Capture> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(captures_dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(captures_dir, ec)) {
        if (!entry.is_directory(ec)) continue;
        const std::string name = entry.path().filename().string();
        // A port writes into <name>.partial and renames when done.
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".partial") == 0) continue;
        out.push_back(ReadCapture(entry.path()));
    }
    std::sort(out.begin(), out.end(), [](const Capture& a, const Capture& b) { return a.id > b.id; });
    return out;
}

bool RemoveCapture(const Capture& capture, std::string& error) {
    std::error_code ec;
    std::filesystem::remove_all(capture.dir, ec);
    if (ec) {
        error = "could not delete " + capture.dir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

}  // namespace launcher
