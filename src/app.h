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
#include "captures.h"
#include "catalog.h"
#include "config.h"
#include "images.h"
#include "installer.h"
#include "launch.h"
#include "theme.h"
#include "toasts.h"
#include "xlive/client.h"

namespace launcher {

// Profile is not on the rail: it opens from a friend's row and Back returns
// to Friends.
// The rail's width; Frame paints its background before DrawRail draws on it.
inline constexpr float kRailWidth = 216.0f;

enum class Tab { Home, Friends, Messages, Invites, Achievements, Issues, Support, Profile, Account };

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
    // Achievement and title tiles, from the server. Opened by main once the
    // renderer exists.
    ImageCache images;
    // Body, heading and title faces; loaded by main after the config.
    xlive::theme::Fonts fonts;
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
        // Which form the panel shows. Register asks for the optional email;
        // Forgot asks for the gamertag, then the mailed code and a new
        // password.
        enum class Mode { SignIn, Register, Forgot };
        Mode mode = Mode::SignIn;
        char server[256] = {};
        char gamertag[32] = {};
        char password[128] = {};
        // Register only. For password recovery and nothing else; empty
        // means the account cannot be recovered, and the form says so.
        char email[256] = {};
        // Forgot only: the eight letters from the mail.
        char code[32] = {};
        xlive::Client::Ticket ticket = 0;
        // What the ticket is doing, for the status line.
        enum class Pending { SignIn, Register, Forgot, Reset };
        Pending pending = Pending::SignIn;
        std::string error;
        // Forgot: where the code went, masked ("f***@example.org"), once
        // the server has sent it. Non-empty switches the form to the code.
        std::string sent_to;
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

    // A friend's profile page: their gamercard, and their achievements in a
    // title next to this player's own.
    struct ProfileState {
        uint64_t xuid = 0;
        // From the friends list, so the header has a name before the card
        // arrives.
        std::string gamertag;
        xlive::Client::Ticket card_ticket = 0;
        xlive::Client::GamercardResult card;
        bool card_loaded = false;
        std::string card_error;
        // The comparison: the same title read twice, theirs and mine. Two
        // tickets, one screen; it is shown once both have landed.
        uint32_t compare_title = 0;
        xlive::Client::Ticket theirs_ticket = 0;
        xlive::Client::Ticket mine_ticket = 0;
        xlive::Client::TitleInfo theirs;
        xlive::Client::TitleInfo mine;
        bool compare_loaded = false;
        std::string compare_error;
        // 0 every achievement, 1 only what they have and I do not, 2 the
        // reverse.
        int compare_filter = 0;
    } profile;
    void OpenProfile(uint64_t xuid, const std::string& gamertag);
    void LoadCompare(uint32_t title_id);

    // Messages: the inbox (the newest message with each person) and one
    // open conversation (the last 20 with them). A message arriving while
    // that conversation is open re-reads it; otherwise it counts as unread
    // on that person until they are opened, and shows as a notification
    // with a Reply button either way.
    struct MessagesState {
        xlive::Client::Ticket inbox_ticket = 0;
        std::vector<xlive::Client::Message> inbox;
        bool inbox_loaded = false;
        uint64_t peer = 0;
        std::string peer_gamertag;
        xlive::Client::Ticket conversation_ticket = 0;
        std::vector<xlive::Client::Message> conversation;
        bool conversation_loaded = false;
        std::string error;
        // Bytes; the character count is checked at send time.
        char draft[1024] = {};
        xlive::Client::Ticket send_ticket = 0;
        std::map<uint64_t, int> unread;
        bool scroll_to_end = false;
        bool focus_draft = false;
    } messages;
    void RefreshInbox();
    void OpenConversation(uint64_t xuid, const std::string& gamertag);
    void SendDraft();
    int unread_messages() const;

    // -- bug reports ---------------------------------------------------------
    // A port's captures wait under <data dir>/captures/ until the player
    // sends or deletes them here; every player's reports are searchable.
    struct IssuesState {
        std::vector<Capture> captures;
        bool scanned = false;
        // What the left column points at: a capture, or a report.
        int selected_capture = -1;
        int selected_report = -1;
        // The form for the selected capture. Kept per capture id so
        // switching between two does not lose either.
        std::string form_for;
        char title[160] = {};
        char summary[4096] = {};
        char steps[4096] = {};
        xlive::Client::Ticket send_ticket = 0;
        std::string sending;  // the capture id being sent
        std::string error;
        // The search.
        char query[256] = {};
        xlive::Client::Ticket search_ticket = 0;
        std::vector<xlive::Client::Issue> results;
        bool searched = false;
        std::string searched_for;
        std::string search_error;
        bool refresh_after_search = false;
        xlive::Client::Ticket delete_ticket = 0;
    } issues;
    // -- the account screen ------------------------------------------------
    // The few things a player may change about themselves: the recovery
    // email, the country, the password. Opened from the gamercard.
    struct AccountState {
        char email[256] = {};
        bool email_filled = false;  // from identity, once per open
        xlive::Client::Ticket email_ticket = 0;
        std::string email_error;
        char country[4] = {};
        bool country_filled = false;
        xlive::Client::Ticket country_ticket = 0;
        std::string country_error;
        char current[128] = {};
        char next[128] = {};
        char confirm[128] = {};
        xlive::Client::Ticket password_ticket = 0;
        std::string password_error;
    } account;
    void OpenAccount();
    void SaveEmail(bool clear);
    void SaveCountry(bool clear);
    void ChangePassword();

    void RescanCaptures();
    void SelectCapture(int index);
    void SendCapture();
    void DeleteCapture();
    void SearchIssues();
    void DeleteReport(int64_t id);

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
    // Queues a latest-release check for every catalog game; they run one
    // at a time behind the installer. Done on the first frame and every
    // kReleaseCheckInterval after, never by a button.
    void CheckReleases();
    static constexpr double kReleaseCheckInterval = 5.0 * 60.0;
    // When the last round of checks began (ImGui time, seconds), and what
    // went wrong with the last one that did, for a line on the Home tab.
    double last_release_check = -1.0;
    std::string release_check_error;
    bool release_check_running() const;
    // The launcher's own release, checked in the same round. Non-empty
    // when GitHub's latest is not the version this binary was built as.
    std::string launcher_update;  // the tag, "v1.1.0"
    std::string launcher_update_error;  // why the last attempt to apply it failed
    bool launcher_updating() const;
    // Downloads, verifies and unpacks the newer launcher beside this one,
    // swaps it in and quits; the new one starts by itself.
    void UpdateLauncher();
    int TitleIndexForKey(const std::string& key) const;
    // What the install directory holds: "no package yet", "package found",
    // "ready" (the first run has unpacked it).
    std::string PackageState(const TitleEntry& entry) const;

private:
    // Declared before `client` so it outlives the worker that pushes into it.
    std::mutex events_mutex_;
    std::deque<xlive::Event> events_;

    std::vector<xlive::Client::Friend> last_friends_;
    // Who was signed in at the last frame, so a SigninChanged that ends a
    // session can name the saved account that lost its tokens.
    uint64_t last_xuid_ = 0;
    bool friends_baseline_ = false;
    std::vector<PendingSocial> pending_;
    // Installs and checks, one at a time, in the order asked.
    struct InstallJob {
        std::string key;
        bool install = false;
    };
    std::deque<InstallJob> install_queue_;
    // The job the installer is on, so a failed check stays quiet and a
    // failed install does not.
    InstallJob current_job_;
    double next_release_check_ = 0.0;
    // Updates already announced with a toast, as key + tag, so a check
    // every few minutes says it once.
    std::set<std::string> announced_updates_;
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
