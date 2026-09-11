// The launcher: one xlive::Client in launcher mode, a queue of its events,
// the screens, the toasts, and the game it started.
//
// Everything the screens touch lives here so that a screen is a function of
// the App and nothing else. The library's events arrive on ITS worker thread
// and are queued; the UI thread drains them once a frame, and nothing outside
// that frame ever touches ImGui.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "accounts.h"
#include "catalog.h"
#include "config.h"
#include "installer.h"
#include "launch.h"
#include "toasts.h"
#include "xlive/client.h"

namespace launcher {

enum class Tab { Home, Friends, Invites, Achievements };

// A ticket the UI issued for a fire-and-forget action. Every one is collected
// — the library holds a result until someone does — and a failure is toasted
// with the server's own code. Never assume success.
struct PendingSocial {
    xlive::Client::Ticket ticket = 0;
    std::string what;
};

class App {
public:
    App();
    ~App();

    // Loads the config, sets the environment the library reads, starts the
    // client. False only when nothing could be done at all.
    bool Init(std::string& error);
    // One frame: drain events, poll tickets, poll the game, draw.
    void Frame();

    bool quit = false;

    // -- shared with the screens --------------------------------------------

    Config config;
    std::string config_error;
    std::unique_ptr<xlive::Client> client;
    Toasts toasts;
    Tab tab = Tab::Home;

    // "We know who the player is" — the cached identity counts, so a saved
    // session with the server briefly unreachable shows the home screen with
    // a status line rather than a sign-in form.
    bool signed_in() const;
    std::string gamertag() const;
    // Tears the client down and starts a fresh one against config.server.
    void RestartClient();
    void SaveConfigOrToast();

    // -- saved accounts ----------------------------------------------------
    Accounts accounts;
    // The sign-in screen shown while already signed in, to add an account.
    bool adding_account = false;
    // Swaps the saved account's tokens into session.json and restarts the
    // client. Refused while a game the launcher started is running: that
    // game would write its own refreshed tokens back over the new ones.
    bool SwitchAccount(uint64_t xuid, std::string& error);
    // Sign out of the current account: its saved entry keeps the gamertag
    // and loses the tokens (the server revokes them).
    void SignOut();
    void ForgetAccount(uint64_t xuid);

    struct SignInState {
        char server[256] = {};
        char gamertag[32] = {};
        char password[128] = {};
        xlive::Client::Ticket ticket = 0;
        bool registering = false;
        std::string error;
    } signin;

    struct FriendsState {
        char add_gamertag[32] = {};
        xlive::Client::Ticket presence_ticket = 0;
        // The player's own presence — really the GAME's — read when the tab
        // opens. Its session_id is what an invite names.
        xlive::Client::Presence mine;
        bool mine_loaded = false;
        bool open = false;
    } friends;

    struct AchievementsState {
        int title_index = -1;
        xlive::Client::Ticket ticket = 0;
        xlive::Client::TitleResult result;
        bool loaded = false;
        std::string error;
    } achievements;

    // Records a ticket to be collected. `what` names the action for the
    // failure toast ("Add friend", "Send invite").
    void Issue(xlive::Client::Ticket ticket, std::string what);
    void RefreshMyPresence();
    void LoadAchievements(int title_index);

    // Accepts, then launches the title if it is configured and not already
    // running. Used by the Invites tab and by the toast's button.
    void AcceptInvite(const xlive::Client::Invite& invite);
    void DeclineInvite(uint64_t invite_id);

    // The one game at a time this launcher started.
    Process game;
    int running_title = -1;  // index into config.titles, or -1
    bool Launch(int title_index, std::string& error);
    std::string TitleName(uint32_t title_id) const;

    // -- installing from the catalog ------------------------------------------
    Installer installer;
    // The latest tag GitHub reported per catalog key, once asked.
    std::map<std::string, std::string> latest_tags;
    // The release page for that tag, for a "Release notes" button.
    std::map<std::string, std::string> release_pages;
    // The last failure per key, shown on the card until the next attempt.
    std::map<std::string, std::string> install_errors;
    void InstallGame(const CatalogGame& game);
    void CheckGame(const CatalogGame& game);
    // Queues a latest-release check for every installed catalog game; they
    // run one at a time behind the installer.
    void CheckInstalledGames();
    int TitleIndexForKey(const std::string& key) const;
    // What the install directory holds: "no package yet", "package found",
    // "ready" (the first run has unpacked it).
    std::string PackageState(const TitleEntry& entry) const;

private:
    // Declared before `client` so it outlives the worker that pushes into it.
    std::mutex events_mutex_;
    std::deque<xlive::Event> events_;

    std::vector<xlive::Client::Friend> last_friends_;
    bool friends_baseline_ = false;
    std::vector<PendingSocial> pending_;
    // Installs and checks, one at a time, in the order asked.
    struct InstallJob {
        std::string key;
        bool install = false;
    };
    std::deque<InstallJob> install_queue_;
    bool checked_installed_ = false;
    void QueueInstallJob(const std::string& key, bool install);
    void PollInstaller();

    void StartClient();
    void DrainEvents();
    void HandleEvent(const xlive::Event& event);
    void PollPending();
    void PollGame();
    void DrawRail();
    void DrawContent();
};

}  // namespace launcher
