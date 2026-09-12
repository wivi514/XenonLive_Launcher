#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "paths.h"
#include "screens/screens.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace launcher {

namespace {

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

const char* LevelName(xlive::LogLevel level) {
    switch (level) {
        case xlive::LogLevel::Debug:   return "debug";
        case xlive::LogLevel::Info:    return "info";
        case xlive::LogLevel::Warning: return "warn";
        case xlive::LogLevel::Error:   return "error";
    }
    return "?";
}

}  // namespace

App::App() = default;

App::~App() {
    // The client's worker pushes into events_; stop it before anything else
    // goes away. A running game is left alone (see Process::~Process).
    client.reset();
}

// A release build links libcurl and OpenSSL statically, and a static OpenSSL
// only knows the certificate directory of the machine that BUILT it. So the
// system's CA bundle is found here, at run time, and named to libxlive and
// to this launcher's own curl handles through XLIVE_CA_FILE — unless the
// player already set it. A dev build against the system libcurl needs none
// of this and is not harmed by it.
static void FindCaBundle() {
#ifndef _WIN32
    if (!xlive::Env("XLIVE_CA_FILE").empty()) return;
    static const char* const candidates[] = {
        "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu, Arch, SteamOS
        "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora, RHEL
        "/etc/ssl/ca-bundle.pem",              // openSUSE
        "/etc/ssl/cert.pem",                   // Alpine, macOS
        "/etc/pki/tls/cacert.pem",
    };
    std::error_code ec;
    for (const char* path : candidates) {
        if (std::filesystem::is_regular_file(path, ec)) {
            SetEnv("XLIVE_CA_FILE", path);
            return;
        }
    }
    std::fprintf(stderr, "[launcher] no CA bundle found; HTTPS may fail to verify\n");
#endif
}

bool App::Init(std::string& error) {
    FindCaBundle();
    if (!LoadConfig(config, config_error)) {
        // Reported on the home screen; the defaults still start.
        std::fprintf(stderr, "[launcher] %s\n", config_error.c_str());
    }
    std::snprintf(signin.server, sizeof(signin.server), "%s", config.server.c_str());

    // The library reads this from the environment, not an option, so it is
    // set before Start() — and inherited by every title launched from here.
    if (config.allow_insecure) SetEnv("XLIVE_ALLOW_INSECURE", "1");

    accounts.Open(DataDir() / "launcher", DataDir() / "session.json");
    StartClient();
    if (!client) {
        error = "the XenonLive client could not start";
        return false;
    }
    return true;
}

void App::StartClient() {
    client.reset();
    auto fresh = std::make_unique<xlive::Client>();

    xlive::Options options;
    options.launcher = true;
    options.title_id = 0;
    options.server_url = config.server;
    // data_dir stays the default so session.json lands where the games look;
    // XLIVE_DATA_DIR overrides it for the library and for us alike.
    options.log = [](xlive::LogLevel level, const std::string& line) {
        std::fprintf(stderr, "[xlive %s] %s\n", LevelName(level), line.c_str());
    };
    options.on_event = [this](const xlive::Event& event) {
        // The worker thread. Queue it; the frame drains it.
        std::lock_guard<std::mutex> lock(events_mutex_);
        events_.push_back(event);
    };
    if (!fresh->Start(std::move(options))) {
        std::fprintf(stderr, "[launcher] xlive::Client::Start refused\n");
        return;
    }
    client = std::move(fresh);
    // Which saved account this session belongs to, before the identity is
    // known: if the server ends it on the first exchange, the SigninChanged
    // that follows must still know whose tokens died.
    last_xuid_ = 0;
    {
        SavedAccount current;
        if (accounts.ReadSession(current)) {
            for (const SavedAccount& saved : accounts.list()) {
                if (saved.has_tokens() && saved.refresh_token == current.refresh_token) {
                    last_xuid_ = saved.xuid;
                }
            }
        }
    }
    last_friends_.clear();
    friends_baseline_ = false;
    friends = FriendsState{};
    achievements = AchievementsState{};
    pending_.clear();
    signin.ticket = 0;
    signin.error.clear();
}

void App::RestartClient() {
    config.server = signin.server;
    SaveConfigOrToast();
    images.set_server(config.server);
    StartClient();
}

void App::SaveConfigOrToast() {
    std::string error;
    if (!SaveConfig(config, error)) toasts.Push("Could not save launcher.json: " + error);
}

// -- saved accounts -----------------------------------------------------------

bool App::SwitchAccount(uint64_t xuid, std::string& error) {
    const SavedAccount* chosen = accounts.Find(xuid);
    if (!chosen || !chosen->has_tokens()) {
        error = "that account needs its password again";
        return false;
    }
    if (game.running()) {
        error = "quit the running game first: it holds the current account's session";
        return false;
    }
    // The current account's latest tokens, before its file is replaced.
    if (client && signed_in() && signin.ticket == 0) accounts.Sync(client->identity().xuid);
    if (!accounts.WriteSession(*chosen)) {
        error = "could not write session.json";
        return false;
    }
    if (!chosen->server.empty() && chosen->server != config.server) {
        config.server = chosen->server;
        std::snprintf(signin.server, sizeof(signin.server), "%s", config.server.c_str());
        SaveConfigOrToast();
        images.set_server(config.server);
    }
    adding_account = false;
    StartClient();
    return true;
}

void App::SignOut() {
    if (!client) return;
    if (signed_in()) accounts.ClearTokens(client->identity().xuid);
    adding_account = false;
    client->SignOut();
}

void App::ForgetAccount(uint64_t xuid) {
    if (client && signed_in() && client->identity().xuid == xuid) {
        adding_account = false;
        client->SignOut();
    }
    accounts.Forget(xuid);
}

bool App::signed_in() const {
    return client && client->identity().xuid != xlive::kOfflineXuid;
}

std::string App::gamertag() const {
    return client ? client->identity().gamertag : std::string(xlive::kOfflineGamertag);
}

std::string App::TitleName(uint32_t title_id) const {
    if (const TitleEntry* entry = config.FindTitle(title_id)) return entry->name;
    return "title " + TitleIdHex(title_id);
}

// -- tickets ------------------------------------------------------------------

void App::Issue(xlive::Client::Ticket ticket, std::string what) {
    if (ticket == 0) return;
    pending_.push_back({ticket, std::move(what)});
}

void App::PollPending() {
    if (!client) return;
    for (size_t i = 0; i < pending_.size();) {
        xlive::Client::SocialResult result;
        const auto status = client->Poll(pending_[i].ticket, result);
        if (status == xlive::Client::OpStatus::Pending) {
            ++i;
            continue;
        }
        if (status == xlive::Client::OpStatus::Failed) {
            toasts.Push(pending_[i].what + " failed: " + result.error);
        } else if (status == xlive::Client::OpStatus::Unknown) {
            // Our bookkeeping, not the server's refusal. Say so, quietly.
            std::fprintf(stderr, "[launcher] ticket for '%s' vanished\n",
                         pending_[i].what.c_str());
        }
        pending_.erase(pending_.begin() + long(i));
    }

    if (friends.presence_ticket != 0) {
        xlive::Client::SocialResult result;
        const auto status = client->Poll(friends.presence_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            friends.presence_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                friends.mine = result.presence;
                friends.mine_loaded = true;
            } else {
                friends.mine = xlive::Client::Presence{};
                friends.mine_loaded = false;
            }
        }
    }

    if (achievements.ticket != 0) {
        xlive::Client::TitleResult result;
        const auto status = client->Poll(achievements.ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            achievements.ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                achievements.result = std::move(result);
                achievements.loaded = true;
                achievements.error.clear();
            } else {
                achievements.loaded = false;
                achievements.error = result.error.empty() ? "unknown" : result.error;
            }
        }
    }

    if (profile.card_ticket != 0) {
        xlive::Client::GamercardResult result;
        const auto status = client->Poll(profile.card_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            profile.card_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                profile.card = std::move(result);
                profile.card_loaded = true;
                profile.card_error.clear();
                if (!profile.card.card.gamertag.empty()) profile.gamertag = profile.card.card.gamertag;
            } else {
                profile.card_loaded = false;
                profile.card_error = result.error.empty() ? "unknown" : result.error;
            }
        }
    }
    // The two halves of a comparison. Whichever fails first names the
    // error; the other is forgotten rather than left to land on a screen
    // that has moved on.
    const auto poll_half = [&](xlive::Client::Ticket& ticket, xlive::Client::TitleInfo& into,
                               xlive::Client::Ticket& other) {
        if (ticket == 0) return;
        xlive::Client::TitleResult result;
        const auto status = client->Poll(ticket, result);
        if (status == xlive::Client::OpStatus::Pending) return;
        ticket = 0;
        if (status == xlive::Client::OpStatus::Succeeded) {
            into = std::move(result.title);
            if (profile.theirs_ticket == 0 && profile.mine_ticket == 0 &&
                profile.compare_error.empty()) {
                profile.compare_loaded = true;
            }
        } else {
            profile.compare_error = result.error.empty() ? "unknown" : result.error;
            if (other != 0) {
                client->Forget(other);
                other = 0;
            }
        }
    };
    poll_half(profile.theirs_ticket, profile.theirs, profile.mine_ticket);
    poll_half(profile.mine_ticket, profile.mine, profile.theirs_ticket);

    if (messages.inbox_ticket != 0) {
        xlive::Client::SocialResult result;
        const auto status = client->Poll(messages.inbox_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            messages.inbox_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                messages.inbox = std::move(result.messages);
                messages.inbox_loaded = true;
            }
        }
    }
    if (messages.conversation_ticket != 0) {
        xlive::Client::SocialResult result;
        const auto status = client->Poll(messages.conversation_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            messages.conversation_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                messages.conversation = std::move(result.messages);
                messages.conversation_loaded = true;
                messages.scroll_to_end = true;
                messages.error.clear();
            } else {
                messages.error = result.error.empty() ? "unknown" : result.error;
            }
        }
    }
    if (messages.send_ticket != 0) {
        xlive::Client::SocialResult result;
        const auto status = client->Poll(messages.send_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            messages.send_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                std::memset(messages.draft, 0, sizeof(messages.draft));
                messages.focus_draft = true;
                if (!result.messages.empty()) {
                    messages.conversation.push_back(result.messages.front());
                    while (messages.conversation.size() > xlive::Client::kMessagesKept) {
                        messages.conversation.erase(messages.conversation.begin());
                    }
                    messages.scroll_to_end = true;
                }
                RefreshInbox();
            } else {
                const std::string why = result.error == "too_long" ? "it is over 256 characters"
                                        : result.error == "not_friends" ? "you are not friends"
                                        : result.error;
                toasts.Push("Message not sent: " + why, 6.0);
            }
        }
    }

    if (signin.ticket != 0) {
        xlive::Client::SocialResult result;
        const auto status = client->Poll(signin.ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            signin.ticket = 0;
            if (status != xlive::Client::OpStatus::Succeeded) {
                signin.error = result.error.empty() ? "unknown" : result.error;
            } else if (signin.pending == SignInState::Pending::Forgot) {
                // The mail is out; the form now wants the code.
                signin.error.clear();
                signin.sent_to = result.detail.empty() ? "your email" : result.detail;
            } else {
                // Signed in, registered, or reset (which signs in too).
                signin.error.clear();
                signin.mode = SignInState::Mode::SignIn;
                signin.sent_to.clear();
                std::memset(signin.password, 0, sizeof(signin.password));
                std::memset(signin.email, 0, sizeof(signin.email));
                std::memset(signin.code, 0, sizeof(signin.code));
                adding_account = false;
            }
        }
    }
}

void App::RefreshMyPresence() {
    if (!client || !signed_in() || friends.presence_ticket != 0) return;
    friends.presence_ticket = client->LoadMyPresence();
}

void App::LoadAchievements(int title_index) {
    if (!client || title_index < 0 || title_index >= int(config.titles.size())) return;
    if (achievements.ticket != 0) client->Forget(achievements.ticket);
    achievements.title_index = title_index;
    achievements.loaded = false;
    achievements.error.clear();
    achievements.ticket = client->LoadTitle(config.titles[size_t(title_index)].title_id);
}

void App::OpenProfile(uint64_t xuid, const std::string& gamertag) {
    if (!client || xuid == 0) return;
    if (profile.card_ticket != 0) client->Forget(profile.card_ticket);
    if (profile.theirs_ticket != 0) client->Forget(profile.theirs_ticket);
    if (profile.mine_ticket != 0) client->Forget(profile.mine_ticket);
    profile = ProfileState{};
    profile.xuid = xuid;
    profile.gamertag = gamertag;
    profile.card_ticket = client->LoadGamercard(xuid);
    tab = Tab::Profile;
}

void App::LoadCompare(uint32_t title_id) {
    if (!client || profile.xuid == 0) return;
    if (profile.theirs_ticket != 0) client->Forget(profile.theirs_ticket);
    if (profile.mine_ticket != 0) client->Forget(profile.mine_ticket);
    profile.compare_title = title_id;
    profile.compare_loaded = false;
    profile.compare_error.clear();
    profile.theirs = xlive::Client::TitleInfo{};
    profile.mine = xlive::Client::TitleInfo{};
    profile.theirs_ticket = client->LoadPlayerTitle(profile.xuid, title_id);
    profile.mine_ticket = client->LoadTitle(title_id);
}

// -- messages -------------------------------------------------------------------

void App::RefreshInbox() {
    if (!client || !signed_in() || messages.inbox_ticket != 0) return;
    messages.inbox_ticket = client->LoadConversations();
}

void App::OpenConversation(uint64_t xuid, const std::string& gamertag) {
    if (!client || xuid == 0) return;
    if (messages.conversation_ticket != 0) client->Forget(messages.conversation_ticket);
    if (messages.peer != xuid) {
        std::memset(messages.draft, 0, sizeof(messages.draft));
        messages.conversation.clear();
        messages.conversation_loaded = false;
    }
    messages.peer = xuid;
    messages.peer_gamertag = gamertag;
    messages.error.clear();
    messages.unread.erase(xuid);
    messages.conversation_ticket = client->LoadConversation(xuid);
    messages.focus_draft = true;
    tab = Tab::Messages;
}

void App::SendDraft() {
    if (!client || messages.peer == 0 || messages.send_ticket != 0) return;
    // Trim trailing whitespace and newlines; an empty draft is not a send.
    std::string body = messages.draft;
    while (!body.empty() && (body.back() == ' ' || body.back() == '\n' || body.back() == '\r')) {
        body.pop_back();
    }
    if (body.empty()) return;
    messages.send_ticket = client->SendMessage(messages.peer, body);
}

int App::unread_messages() const {
    int n = 0;
    for (const auto& [xuid, count] : messages.unread) n += count;
    return n;
}

// -- invites ------------------------------------------------------------------

void App::AcceptInvite(const xlive::Client::Invite& invite) {
    if (!client) return;
    Issue(client->AcceptInvite(invite.id), "Accept invite");

    // If the game is already running, the server pushes "invite_taken" to
    // it, the library raises InviteAccepted, and the port posts the
    // notification — nothing more to do here. If it is not, launch it: its
    // own libxlive finds the accepted invitation at sync and the port answers
    // XInviteGetAcceptedInfo from it.
    int index = -1;
    for (size_t i = 0; i < config.titles.size(); ++i) {
        if (config.titles[i].title_id == invite.title_id) index = int(i);
    }
    const std::string title = TitleName(invite.title_id);
    if (game.running()) {
        if (running_title == index) {
            toasts.Push("Accepted. " + title + " has been told; it joins from there.");
        } else {
            toasts.Push("Accepted, but a different title is running. Quit it and press "
                        "Play on " + title + " to join.", 8.0);
        }
        return;
    }
    if (index < 0) {
        toasts.Push("Accepted " + invite.from_gamertag + "'s invitation. Add " + title +
                    " on the Home tab to launch it.");
        return;
    }
    std::string error;
    if (Launch(index, error)) {
        toasts.Push("Launching " + title + " to join " + invite.from_gamertag);
    } else {
        toasts.Push("Could not launch " + title + ": " + error, 8.0);
    }
}

void App::DeclineInvite(uint64_t invite_id) {
    if (!client) return;
    Issue(client->DeclineInvite(invite_id), "Decline invite");
}

// -- the game -----------------------------------------------------------------

bool App::Launch(int title_index, std::string& error) {
    if (title_index < 0 || title_index >= int(config.titles.size())) {
        error = "no such title";
        return false;
    }
    if (game.running()) {
        error = config.titles[size_t(running_title)].name + " is already running";
        return false;
    }
    const TitleEntry& entry = config.titles[size_t(title_index)];
    if (entry.exe.empty()) {
        error = "no executable configured";
        return false;
    }
    std::map<std::string, std::string> env = entry.env;
    if (config.allow_insecure) env["XLIVE_ALLOW_INSECURE"] = "1";
#ifndef _WIN32
    // An AppImage mounts itself with FUSE. Without /dev/fuse the type-2
    // runtime can extract itself to a temporary directory and run from there
    // instead, if told to; a machine with no FUSE is not a machine that
    // cannot play.
    if (entry.managed() && ::access("/dev/fuse", F_OK) != 0) {
        env["APPIMAGE_EXTRACT_AND_RUN"] = "1";
    }
#endif
    if (!game.Start(entry.exe, entry.cwd, env, error)) return false;
    running_title = title_index;
    return true;
}

void App::PollGame() {
    if (!game.running()) return;
    game.Poll();
    if (game.running()) return;
    const std::string name =
        running_title >= 0 && running_title < int(config.titles.size())
            ? config.titles[size_t(running_title)].name
            : "The game";
    if (game.exited_abnormally()) {
        toasts.Push(name + " stopped on signal " + std::to_string(game.exit_code()), 8.0);
    } else if (game.exit_code() != 0) {
        toasts.Push(name + " exited with code " + std::to_string(game.exit_code()), 8.0);
    } else {
        toasts.Push(name + " exited");
    }
    running_title = -1;
}

// -- installing from the catalog ------------------------------------------------

int App::TitleIndexForKey(const std::string& key) const {
    for (size_t i = 0; i < config.titles.size(); ++i) {
        if (config.titles[i].key == key) return int(i);
    }
    return -1;
}

void App::QueueInstallJob(const std::string& key, bool install) {
    for (InstallJob& job : install_queue_) {
        if (job.key != key) continue;
        // An install supersedes a check of the same game; a second check
        // adds nothing.
        job.install = job.install || install;
        return;
    }
    install_queue_.push_back({key, install});
}

void App::InstallGame(const CatalogGame& item) {
    if (running_title >= 0 && game.running() &&
        config.titles[size_t(running_title)].key == item.key) {
        toasts.Push(std::string(item.name) + " is running; quit it before updating");
        return;
    }
    install_errors.erase(item.key);
    QueueInstallJob(item.key, true);
}

void App::CheckGame(const CatalogGame& item) { QueueInstallJob(item.key, false); }

void App::CheckInstalledGames() {
    for (const TitleEntry& entry : config.titles) {
        if (entry.managed() && CatalogByKey(entry.key)) QueueInstallJob(entry.key, false);
    }
}

std::string App::PackageState(const TitleEntry& entry) const {
    std::error_code ec;
    const std::filesystem::path assets = std::filesystem::path(entry.cwd) / "assets";
    if (std::filesystem::is_directory(assets / "game", ec)) return "ready";
    const std::filesystem::path package = assets / "package";
    if (std::filesystem::is_directory(package, ec)) {
        for (const auto& item : std::filesystem::directory_iterator(package, ec)) {
            if (!item.is_regular_file(ec)) continue;
            // The port seeds a PUT_YOUR_GAME_HERE.txt; the package itself is
            // the console's hash-named file with no extension.
            if (item.path().extension() == ".txt") continue;
            return "package found";
        }
    }
    return "no package yet";
}

void App::PollInstaller() {
    // A finished job is consumed BEFORE the next one starts: starting a job
    // resets the progress, and a result nobody read is an install the
    // config never learns about.
    const InstallProgress progress = installer.Poll();
    const bool finished = !installer.busy() && (progress.phase == InstallPhase::Done ||
                                                progress.phase == InstallPhase::Failed);
    if (finished) {
        const CatalogGame* item = CatalogByKey(progress.key);
        std::fprintf(stderr, "[installer] %s: %s %s\n", progress.key.c_str(),
                     PhaseName(progress.phase), progress.message.c_str());
        if (progress.phase == InstallPhase::Failed) {
            install_errors[progress.key] = progress.message;
            if (item) toasts.Push(std::string(item->name) + ": " + progress.message, 8.0);
        } else if (item) {
            latest_tags[progress.key] = progress.release.tag;
            release_pages[progress.key] = progress.release.html_url;
            if (progress.installed) {
                // The install becomes an ordinary title: everything else —
                // Play, invites, achievements — works off the entry from here.
                const std::filesystem::path dir = config.InstallDir(item->key);
                int index = TitleIndexForKey(item->key);
                if (index < 0) {
                    config.titles.push_back(TitleEntry{});
                    index = int(config.titles.size()) - 1;
                }
                TitleEntry& entry = config.titles[size_t(index)];
                entry.key = item->key;
                entry.version = progress.release.tag;
                entry.title_id = item->title_id;
                entry.name = item->name;
                entry.exe = (dir / PlatformExecutable(*item)).string();
                entry.cwd = dir.string();
                // The shipped *_defaults.env already turns the renderer and
                // the port's own pre-boot window on; these are the XenonLive
                // half.
                entry.env[std::string(item->prefix) + "_XLIVE_ONLINE"] = "1";
                entry.env[std::string(item->prefix) + "_XLIVE_COOP"] = "1";
                SaveConfigOrToast();
                toasts.Push(std::string(item->name) + " " + progress.release.tag + " installed", 6.0);
            }
        }
        installer.Acknowledge();
    }

    if (!installer.busy() && !install_queue_.empty()) {
        const InstallJob job = install_queue_.front();
        install_queue_.pop_front();
        if (const CatalogGame* item = CatalogByKey(job.key)) {
            if (job.install) {
                installer.Install(*item, config.InstallDir(item->key));
            } else {
                installer.CheckLatest(*item);
            }
        }
    }
}

// -- events -------------------------------------------------------------------

void App::DrainEvents() {
    std::deque<xlive::Event> batch;
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        batch.swap(events_);
    }
    for (const xlive::Event& event : batch) HandleEvent(event);
}

void App::HandleEvent(const xlive::Event& event) {
    using xlive::EventKind;
    switch (event.kind) {
        case EventKind::SigninChanged:
            if (signed_in()) {
                toasts.Push("Signed in as " + gamertag());
                last_xuid_ = client->identity().xuid;
                // A friends tab opened before the identity was known asked
                // about nobody; ask again now that there is someone.
                if (tab == Tab::Friends) RefreshMyPresence();
            } else {
                // Either our own Sign out, or the server ending the session
                // (a dead refresh token). The saved account keeps its name
                // and loses the tokens either way.
                if (last_xuid_ != 0) accounts.ClearTokens(last_xuid_);
                last_xuid_ = 0;
                toasts.Push("Signed out: " + client->status(), 6.0);
                last_friends_.clear();
                friends_baseline_ = false;
                friends.mine = xlive::Client::Presence{};
                friends.mine_loaded = false;
                achievements.loaded = false;
                if (tab == Tab::Profile) tab = Tab::Friends;
                profile = ProfileState{};
                messages = MessagesState{};
            }
            break;

        case EventKind::ConnectionChanged:
            if (client && client->online()) {
                toasts.Push("Connected to " + config.server);
            } else {
                toasts.Push("Connection lost; retrying");
            }
            break;

        case EventKind::FriendsChanged: {
            // Diff against the last list to say who came online, the way the
            // port's XliveSocial_OnFriendsChanged does. The first list after
            // a sign-in is the baseline, not ten people "coming online".
            if (!client) break;
            const auto now = client->friends();
            if (friends_baseline_) {
                for (const auto& f : now) {
                    const xlive::Client::Friend* was = nullptr;
                    for (const auto& old : last_friends_) {
                        if (old.xuid == f.xuid) was = &old;
                    }
                    using R = xlive::Client::Relation;
                    if (f.relation == R::RequestReceived &&
                        (!was || was->relation != R::RequestReceived)) {
                        toasts.Push(f.gamertag + " wants to be your friend");
                    } else if (f.relation == R::Friend && was && was->relation == R::RequestSent) {
                        toasts.Push(f.gamertag + " accepted your friend request");
                    }
                    if (f.presence.online() && (!was || !was->presence.online())) {
                        if (f.presence.state == xlive::Client::PresenceState::Playing) {
                            toasts.Push(f.gamertag + " is playing " + f.presence.title_name);
                        } else {
                            toasts.Push(f.gamertag + " is online");
                        }
                    }
                }
            }
            last_friends_ = now;
            friends_baseline_ = true;
            break;
        }

        case EventKind::InviteReceived: {
            xlive::Client::Invite invite;
            invite.id = event.invite_id;
            invite.from_xuid = event.xuid;
            invite.from_gamertag = event.gamertag;
            invite.title_id = event.title_id;
            invite.session_id = event.session_id;
            toasts.PushWithAction(event.gamertag + " invited you to " + TitleName(event.title_id),
                                  "Accept", [this, invite] { AcceptInvite(invite); });
            break;
        }

        case EventKind::InviteAnswered:
            toasts.Push(event.gamertag + (event.accepted ? " accepted" : " declined") +
                        " your invitation");
            break;

        case EventKind::MessageReceived: {
            const uint64_t from = event.xuid;
            const std::string who = event.gamertag;
            if (tab == Tab::Messages && messages.peer == from) {
                // The open conversation: re-read it so the new one has its
                // id and time, and the window stays at 20.
                if (messages.conversation_ticket == 0) {
                    messages.conversation_ticket = client->LoadConversation(from);
                }
            } else {
                messages.unread[from] += 1;
            }
            RefreshInbox();
            toasts.PushWithAction(who + ": " + event.message, "Reply",
                                  [this, from, who] { OpenConversation(from, who); }, 8.0);
            break;
        }

        case EventKind::AchievementUnlocked:
            // Only a title client sees this; kept for the day the library
            // forwards it.
            toasts.Push("Achievement unlocked: " + event.achievement_name);
            break;

        case EventKind::InviteAccepted:
            // The game's side of an acceptance; never delivered to a
            // launcher.
            break;
    }
}

// -- the frame ----------------------------------------------------------------

void App::Frame() {
    DrainEvents();
    PollPending();
    PollGame();
    PollInstaller();
    if (!checked_installed_) {
        checked_installed_ = true;
        CheckInstalledGames();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("XenonLive", nullptr, flags);
    ImGui::PopStyleVar(2);

    // The active account's saved copy follows session.json — the library
    // rotates the tokens in it — except while a sign-in is in flight, when
    // the file already holds the NEXT account's tokens and the identity has
    // not caught up yet.
    if (client && signed_in() && signin.ticket == 0) {
        const xlive::Identity id = client->identity();
        last_xuid_ = id.xuid;
        if (!accounts.Find(id.xuid)) {
            // First time this account is seen from here: a fresh sign-in,
            // or a session.json from before saved accounts existed.
            SavedAccount fresh;
            if (accounts.ReadSession(fresh)) {
                fresh.xuid = id.xuid;
                fresh.gamertag = id.gamertag;
                if (fresh.server.empty()) fresh.server = config.server;
                accounts.Save(fresh);
            }
        } else {
            accounts.Sync(id.xuid);
        }
    }

    const bool show_signin = !signed_in() || signin.ticket != 0 || adding_account;
    if (show_signin) {
        DrawSignIn(*this);
    } else {
        DrawRail();
        ImGui::SameLine();
        DrawContent();
    }
    ImGui::End();

    toasts.Draw();
}

void App::DrawRail() {
    namespace theme = xlive::theme;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kRail);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 14.0f));
    ImGui::BeginChild("rail", ImVec2(210.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    // The wordmark, lime on charcoal, the way the dashboard's was.
    ImGui::PushFont(fonts.title);
    ImGui::TextColored(theme::kLime, "Xenon");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted("Live");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    const auto blade = [&](const char* label, Tab which, const char* badge = nullptr) {
        const bool selected = tab == which || (which == Tab::Friends && tab == Tab::Profile);
        if (theme::RailItem(label, selected, badge)) {
            tab = which;
            if (which == Tab::Achievements && !achievements.loaded && achievements.ticket == 0 &&
                !config.titles.empty()) {
                LoadAchievements(achievements.title_index < 0 ? 0 : achievements.title_index);
            }
        }
    };
    blade("Home", Tab::Home);
    blade("Friends", Tab::Friends);
    char unread[16] = {};
    if (unread_messages() > 0) std::snprintf(unread, sizeof(unread), "%d", unread_messages());
    blade("Messages", Tab::Messages, unread);
    const size_t inbox = client ? client->invites().size() : 0;
    char badge[16] = {};
    if (inbox > 0) std::snprintf(badge, sizeof(badge), "%zu", inbox);
    blade("Invites", Tab::Invites, badge);
    blade("Achievements", Tab::Achievements);
    blade("Support", Tab::Support);

    // The gamercard, at the bottom.
    const float card_h = fonts.heading->FontSize + ImGui::GetTextLineHeight() * 2.0f + 26.0f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - card_h - 14.0f);
    const bool online = client && client->online();
    theme::Gamercard(gamertag().c_str(), client ? client->identity().gamerscore : 0u, online,
                     online ? "online" : "offline");
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::DrawContent() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, xlive::theme::kBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
    ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (tab != Tab::Friends) friends.open = false;
    switch (tab) {
        case Tab::Home:         DrawHome(*this); break;
        case Tab::Friends:      DrawFriends(*this); break;
        case Tab::Messages:     DrawMessages(*this); break;
        case Tab::Invites:      DrawInvites(*this); break;
        case Tab::Achievements: DrawAchievements(*this); break;
        case Tab::Support:      DrawSupport(*this); break;
        case Tab::Profile:      DrawProfile(*this); break;
    }
    ImGui::EndChild();
}

}  // namespace launcher
