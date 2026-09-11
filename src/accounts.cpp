#include "accounts.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "json.h"

namespace launcher {

namespace {

std::string HexXuid(uint64_t xuid) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)xuid);
    return buf;
}

bool ParseHexXuid(const std::string& text, uint64_t& out) {
    if (text.empty() || text.size() > 16) return false;
    uint64_t value = 0;
    for (char c : text) {
        uint64_t digit;
        if (c >= '0' && c <= '9') digit = uint64_t(c - '0');
        else if (c >= 'a' && c <= 'f') digit = uint64_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = uint64_t(c - 'A' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

bool ReadDoc(const std::filesystem::path& path, xlive::json::Value& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = xlive::json::Parse(text);
    if (!parsed.ok || !parsed.value.is_object()) return false;
    out = parsed.value;
    return true;
}

// A temp file and a rename, like every other write of a token file.
bool WriteDoc(const std::filesystem::path& path, const xlive::json::Value& doc) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::string temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::string text = doc.Serialize();
        out.write(text.data(), std::streamsize(text.size()));
        out.flush();
        if (!out) return false;
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temp, path, ec);
    }
    return !ec;
}

xlive::json::Value AccountDoc(const SavedAccount& account) {
    using xlive::json::Value;
    Value doc = Value::Object();
    doc.Set("xuid", Value::String(HexXuid(account.xuid)));
    doc.Set("gamertag", Value::String(account.gamertag));
    doc.Set("server", Value::String(account.server));
    doc.Set("access_token", Value::String(account.access_token));
    doc.Set("refresh_token", Value::String(account.refresh_token));
    return doc;
}

}  // namespace

std::filesystem::path Accounts::AccountFile(uint64_t xuid) const {
    return dir_ / "accounts" / (HexXuid(xuid) + ".json");
}

void Accounts::Open(const std::filesystem::path& dir, const std::filesystem::path& session) {
    dir_ = dir;
    session_ = session;
    Load();
}

void Accounts::Load() {
    list_.clear();
    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator(dir_ / "accounts", ec)) {
        if (!item.is_regular_file(ec) || item.path().extension() != ".json") continue;
        xlive::json::Value doc;
        if (!ReadDoc(item.path(), doc)) continue;
        SavedAccount account;
        if (!ParseHexXuid(doc["xuid"].AsString(), account.xuid) || account.xuid == 0) continue;
        account.gamertag = doc["gamertag"].AsString();
        account.server = doc["server"].AsString();
        account.access_token = doc["access_token"].AsString();
        account.refresh_token = doc["refresh_token"].AsString();
        list_.push_back(std::move(account));
    }
    std::sort(list_.begin(), list_.end(), [](const SavedAccount& a, const SavedAccount& b) {
        return a.gamertag < b.gamertag;
    });
}

const SavedAccount* Accounts::Find(uint64_t xuid) const {
    for (const SavedAccount& account : list_) {
        if (account.xuid == xuid) return &account;
    }
    return nullptr;
}

bool Accounts::Save(const SavedAccount& account) {
    if (account.xuid == 0) return false;
    if (!WriteDoc(AccountFile(account.xuid), AccountDoc(account))) return false;
    auto found = std::find_if(list_.begin(), list_.end(),
                              [&](const SavedAccount& a) { return a.xuid == account.xuid; });
    if (found != list_.end()) {
        *found = account;
    } else {
        list_.push_back(account);
        std::sort(list_.begin(), list_.end(), [](const SavedAccount& a, const SavedAccount& b) {
            return a.gamertag < b.gamertag;
        });
    }
    return true;
}

void Accounts::Forget(uint64_t xuid) {
    std::error_code ec;
    std::filesystem::remove(AccountFile(xuid), ec);
    list_.erase(std::remove_if(list_.begin(), list_.end(),
                               [&](const SavedAccount& a) { return a.xuid == xuid; }),
                list_.end());
}

void Accounts::ClearTokens(uint64_t xuid) {
    auto found = std::find_if(list_.begin(), list_.end(),
                              [&](const SavedAccount& a) { return a.xuid == xuid; });
    if (found == list_.end()) return;
    found->access_token.clear();
    found->refresh_token.clear();
    WriteDoc(AccountFile(xuid), AccountDoc(*found));
}

bool Accounts::ReadSession(SavedAccount& out) const {
    xlive::json::Value doc;
    if (!ReadDoc(session_, doc)) return false;
    out.server = doc["server"].AsString();
    out.access_token = doc["access_token"].AsString();
    out.refresh_token = doc["refresh_token"].AsString();
    return out.has_tokens();
}

bool Accounts::WriteSession(const SavedAccount& account) const {
    using xlive::json::Value;
    // Exactly what the library writes: the server and the two tokens. Not
    // the gamertag — the library preserves keys it does not know when it
    // rotates the tokens, and a name left behind by a previous account
    // would be a lie in the file.
    Value doc = Value::Object();
    doc.Set("server", Value::String(account.server));
    doc.Set("access_token", Value::String(account.access_token));
    doc.Set("refresh_token", Value::String(account.refresh_token));
    return WriteDoc(session_, doc);
}

void Accounts::Sync(uint64_t xuid) {
    if (xuid == 0) return;
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(session_, ec);
    if (ec || stamp == session_seen_) return;
    session_seen_ = stamp;

    SavedAccount current;
    if (!ReadSession(current)) return;
    auto found = std::find_if(list_.begin(), list_.end(),
                              [&](const SavedAccount& a) { return a.xuid == xuid; });
    if (found == list_.end()) return;
    if (found->access_token == current.access_token &&
        found->refresh_token == current.refresh_token) {
        return;
    }
    found->access_token = current.access_token;
    found->refresh_token = current.refresh_token;
    if (!current.server.empty()) found->server = current.server;
    WriteDoc(AccountFile(xuid), AccountDoc(*found));
}

}  // namespace launcher
