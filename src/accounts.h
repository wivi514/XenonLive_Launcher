// Saved accounts: one token file per account, so a machine shared by two
// players — or one player with two gamertags — can switch without retyping
// a password.
//
// The game reads exactly one file, session.json, and the library rotates the
// tokens in it whenever it refreshes. So the saved copy of the ACTIVE account
// follows session.json (Sync), and switching is: save the current one, write
// the chosen one over session.json, restart the client. Never through
// SignOut(): that revokes the account's tokens on the server, and a saved
// session has to survive not being the active one.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

struct SavedAccount {
    uint64_t xuid = 0;
    std::string gamertag;
    std::string server;
    std::string access_token;
    std::string refresh_token;

    // Signed out, or never signed in from here: the picker offers the
    // gamertag but the player has to give the password again.
    bool has_tokens() const { return !access_token.empty() || !refresh_token.empty(); }
};

class Accounts {
public:
    // dir is the launcher's own directory (data_dir/launcher); the files go
    // in accounts/ under it. session is data_dir/session.json.
    void Open(const std::filesystem::path& dir, const std::filesystem::path& session);

    const std::vector<SavedAccount>& list() const { return list_; }
    const SavedAccount* Find(uint64_t xuid) const;

    // Records `account` (replacing an entry with the same xuid).
    bool Save(const SavedAccount& account);
    void Forget(uint64_t xuid);

    // Reads session.json into `out`. False when there is no usable session.
    bool ReadSession(SavedAccount& out) const;
    // Writes an account's tokens as session.json — the switch.
    bool WriteSession(const SavedAccount& account) const;

    // Copies session.json's tokens into the saved entry for `xuid` when the
    // file has changed since the last look. Cheap: one stat per call.
    void Sync(uint64_t xuid);
    // Drops the tokens of the saved entry, keeping the name.
    void ClearTokens(uint64_t xuid);

private:
    std::filesystem::path AccountFile(uint64_t xuid) const;
    void Load();

    std::filesystem::path dir_;
    std::filesystem::path session_;
    std::vector<SavedAccount> list_;
    std::filesystem::file_time_type session_seen_{};
};

}  // namespace launcher
