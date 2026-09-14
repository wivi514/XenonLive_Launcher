#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "imgui.h"
#include "paths.h"
#include "screens/screens.h"
#include "selfupdate.h"

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
    presence_news_.Reset();
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

    // The account screen's three tickets: one toast or one line each.
    const auto collect = [&](xlive::Client::Ticket& ticket, std::string& error,
                             const char* done, auto on_ok) {
        if (ticket == 0) return;
        xlive::Client::SocialResult result;
        const auto status = client->Poll(ticket, result);
        if (status == xlive::Client::OpStatus::Pending) return;
        ticket = 0;
        if (status == xlive::Client::OpStatus::Succeeded) {
            error.clear();
            toasts.Push(done, 4.0);
            on_ok();
        } else {
            error = result.error.empty() ? "unknown" : result.error;
        }
    };
    collect(account.email_ticket, account.email_error, "Recovery email saved", [&] {
        account.email_filled = false;
    });
    collect(account.country_ticket, account.country_error, "Country saved", [&] {
        account.country_filled = false;
    });
    collect(account.password_ticket, account.password_error,
            "Password changed; every other device is signed out", [&] {
                std::memset(account.current, 0, sizeof(account.current));
                std::memset(account.next, 0, sizeof(account.next));
                std::memset(account.confirm, 0, sizeof(account.confirm));
            });

    if (issues.send_ticket != 0) {
        xlive::Client::IssueResult result;
        const auto status = client->Poll(issues.send_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            issues.send_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                // Sent: the capture leaves the disk, the form clears, and
                // the report shows up in the search at once.
                for (const Capture& c : issues.captures) {
                    if (c.id != issues.sending) continue;
                    std::string error;
                    if (!RemoveCapture(c, error)) toasts.Push(error, 8.0);
                    break;
                }
                issues.form_for.clear();
                std::memset(issues.title, 0, sizeof(issues.title));
                std::memset(issues.summary, 0, sizeof(issues.summary));
                std::memset(issues.steps, 0, sizeof(issues.steps));
                issues.error.clear();
                issues.selected_capture = -1;
                RescanCaptures();
                toasts.Push("Bug report sent: " + result.issue.title, 6.0);
                // The report joins the list on the left. A search still
                // running would overwrite it, so that one is redone.
                if (issues.search_ticket == 0) {
                    issues.results.insert(issues.results.begin(), result.issue);
                    issues.searched = true;
                } else {
                    issues.refresh_after_search = true;
                }
            } else {
                std::string why = result.error;
                if (result.error == "too_many_reports") why = "at most 10 reports a day";
                else if (result.error == "unreadable_file") why = "could not read " + result.failed_file;
                else if (result.error == "too_large" || result.error == "report_too_big") {
                    why = result.failed_file + " is too big (4 MiB a file, 8 MiB a report)";
                } else if (result.error == "not_text_or_image") {
                    why = result.failed_file + " is neither an image nor text";
                }
                issues.error = "Not sent: " + why;
            }
            issues.sending.clear();
        }
    }
    if (issues.search_ticket != 0) {
        xlive::Client::IssueResult result;
        const auto status = client->Poll(issues.search_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            issues.search_ticket = 0;
            issues.searched = true;
            if (status == xlive::Client::OpStatus::Succeeded) {
                issues.results = std::move(result.issues);
                issues.search_error.clear();
            } else {
                issues.search_error = result.error;
            }
            issues.selected_report = -1;
            if (issues.refresh_after_search) {
                issues.refresh_after_search = false;
                SearchIssues();
            }
        }
    }
    if (issues.delete_ticket != 0) {
        xlive::Client::IssueResult result;
        const auto status = client->Poll(issues.delete_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            issues.delete_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                toasts.Push("Report deleted", 4.0);
                issues.detail_for = 0;
                SearchIssues();
            } else {
                toasts.Push("Could not delete the report: " + result.error, 6.0);
            }
        }
    }
    if (issues.detail_ticket != 0) {
        xlive::Client::IssueResult result;
        const auto status = client->Poll(issues.detail_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            issues.detail_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                issues.detail = std::move(result.issue);
                issues.detail_for = issues.detail.id;
                issues.detail_error.clear();
            } else {
                issues.detail_error = result.error == "not_developer"
                                          ? "This account is no longer a developer"
                                          : result.error;
            }
        }
    }
    if (issues.state_ticket != 0) {
        xlive::Client::IssueResult result;
        const auto status = client->Poll(issues.state_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            issues.state_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                // The list on the left and the detail both say the new
                // state at once; the list is refetched so a filter that no
                // longer matches drops it.
                for (auto& is : issues.results) {
                    if (is.id == issues.detail.id) is.state = issues.detail.state;
                }
                if (issues.dev_list) issues.refresh_after_search = true;
                if (issues.search_ticket == 0 && issues.refresh_after_search) {
                    issues.refresh_after_search = false;
                    const int64_t keep = issues.detail_for;
                    SearchIssues();
                    issues.detail_for = keep;
                }
            } else {
                toasts.Push("Could not change the state: " + result.error, 6.0);
                if (issues.detail_for != 0) DevOpenReport(issues.detail_for);
            }
        }
    }
    if (issues.download_ticket != 0) {
        xlive::Client::IssueResult result;
        const auto status = client->Poll(issues.download_ticket, result);
        if (status != xlive::Client::OpStatus::Pending) {
            issues.download_ticket = 0;
            if (status == xlive::Client::OpStatus::Succeeded) {
                // Next file, if any.
                if (!issues.download_queue.empty() && issues.detail_for == issues.downloading) {
                    const std::string name = issues.download_queue.front();
                    issues.download_queue.erase(issues.download_queue.begin());
                    issues.download_ticket = client->DevSaveIssueFile(
                        issues.downloading, name, (DevIssueDir(&issues.detail) / name).string());
                } else {
                    issues.download_queue.clear();
                    issues.downloading = 0;
                }
            } else {
                issues.download_error = "Could not fetch " + result.failed_file + ": " + result.error;
                issues.download_queue.clear();
                issues.downloading = 0;
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
                // The mail is out. Back to the sign-in form, which says
                // where the password went; the player signs in with it.
                signin.error.clear();
                signin.mode = SignInState::Mode::SignIn;
                signin.sent_to = result.detail.empty() ? "your email" : result.detail;
                std::memset(signin.password, 0, sizeof(signin.password));
            } else {
                // Signed in or registered.
                signin.error.clear();
                signin.mode = SignInState::Mode::SignIn;
                signin.sent_to.clear();
                std::memset(signin.password, 0, sizeof(signin.password));
                std::memset(signin.email, 0, sizeof(signin.email));
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

// -- the account screen --------------------------------------------------------

void App::OpenAccount() {
    tab = Tab::Account;
    account.email_filled = false;
    account.country_filled = false;
    account.email_error.clear();
    account.country_error.clear();
    account.password_error.clear();
}

void App::SaveEmail(bool clear) {
    if (!client || account.email_ticket != 0) return;
    xlive::Client::ProfileUpdate update;
    update.email_set = true;
    update.email = clear ? std::string() : std::string(account.email);
    if (clear) std::memset(account.email, 0, sizeof(account.email));
    account.email_error.clear();
    account.email_ticket = client->UpdateProfile(update);
}

void App::SaveCountry(bool clear) {
    if (!client || account.country_ticket != 0) return;
    xlive::Client::ProfileUpdate update;
    update.country_set = true;
    update.country = clear ? std::string() : std::string(account.country);
    if (clear) std::memset(account.country, 0, sizeof(account.country));
    account.country_error.clear();
    account.country_ticket = client->UpdateProfile(update);
}

void App::ChangePassword() {
    if (!client || account.password_ticket != 0) return;
    if (std::strcmp(account.next, account.confirm) != 0) {
        account.password_error = "mismatch";
        return;
    }
    if (std::strlen(account.next) < 8) {
        account.password_error = "bad_password";
        return;
    }
    account.password_error.clear();
    account.password_ticket = client->ChangePassword(account.current, account.next);
}

// -- bug reports --------------------------------------------------------------

void App::RescanCaptures() {
    issues.captures = client ? ScanCaptures(client->captures_dir()) : std::vector<Capture>{};
    issues.scanned = true;
    if (issues.selected_capture >= int(issues.captures.size())) issues.selected_capture = -1;
    // The form follows its capture by id, not by index: a scan that
    // finds a new one first must not hand this form to it.
    if (!issues.form_for.empty()) {
        int at = -1;
        for (size_t i = 0; i < issues.captures.size(); ++i) {
            if (issues.captures[i].id == issues.form_for) at = int(i);
        }
        issues.selected_capture = at;
    }
}

void App::SelectCapture(int index) {
    if (index < 0 || index >= int(issues.captures.size())) {
        issues.selected_capture = -1;
        return;
    }
    issues.selected_capture = index;
    issues.selected_report = -1;
    const std::string& id = issues.captures[size_t(index)].id;
    if (issues.form_for != id) {
        issues.form_for = id;
        std::memset(issues.title, 0, sizeof(issues.title));
        std::memset(issues.summary, 0, sizeof(issues.summary));
        std::memset(issues.steps, 0, sizeof(issues.steps));
        issues.error.clear();
    }
}

void App::SendCapture() {
    if (!client || issues.send_ticket != 0 || issues.selected_capture < 0 ||
        issues.selected_capture >= int(issues.captures.size())) {
        return;
    }
    const Capture& c = issues.captures[size_t(issues.selected_capture)];
    if (!c.problem.empty()) {
        issues.error = "This capture cannot be sent: " + c.problem;
        return;
    }
    xlive::Client::IssueDraft draft;
    draft.title_id = c.title_id;
    draft.game_version = c.game_version;
    draft.title = issues.title;
    draft.summary = issues.summary;
    draft.steps = issues.steps;
    draft.captured_at = c.captured_at;
    draft.system_json = c.system_json;
    for (const CaptureFile& f : c.files) draft.files.push_back({f.name, f.path.string()});
    // Trimmed the way the server trims, so "   " is caught here.
    const auto blank = [](const std::string& s) {
        return s.find_first_not_of(" \t\r\n") == std::string::npos;
    };
    if (blank(draft.title)) {
        issues.error = "Give it a title.";
        return;
    }
    if (blank(draft.summary)) {
        issues.error = "Say what happened.";
        return;
    }
    issues.error.clear();
    issues.sending = c.id;
    issues.send_ticket = client->ReportIssue(draft);
}

void App::DeleteCapture() {
    if (issues.selected_capture < 0 || issues.selected_capture >= int(issues.captures.size())) return;
    const Capture c = issues.captures[size_t(issues.selected_capture)];
    std::string error;
    if (!RemoveCapture(c, error)) {
        toasts.Push(error, 8.0);
        return;
    }
    if (issues.form_for == c.id) {
        issues.form_for.clear();
        std::memset(issues.title, 0, sizeof(issues.title));
        std::memset(issues.summary, 0, sizeof(issues.summary));
        std::memset(issues.steps, 0, sizeof(issues.steps));
    }
    issues.selected_capture = -1;
    issues.error.clear();
    RescanCaptures();
}

void App::SearchIssues() {
    if (!client || !signed_in() || issues.search_ticket != 0) return;
    issues.searched_for = issues.query;
    // A developer with nothing to search for gets every report, newest
    // first, filtered by state — the queue to work through. Words always
    // mean a search, for everyone.
    const bool blank = std::string(issues.query).find_first_not_of(" \t\r\n") == std::string::npos;
    issues.dev_list = developer() && blank;
    issues.search_ticket = issues.dev_list ? client->DevListIssues(issues.dev_filter, 100)
                                           : client->SearchIssues(issues.query);
}

void App::DeleteReport(int64_t id) {
    if (!client || issues.delete_ticket != 0) return;
    issues.delete_ticket = client->DeleteIssue(id);
}

// -- the developer's side --------------------------------------------------

bool App::developer() const { return client && signed_in() && client->identity().developer; }

void App::DevOpenReport(int64_t id) {
    if (!developer() || issues.detail_ticket != 0) return;
    issues.detail_error.clear();
    issues.detail_ticket = client->DevGetIssue(id);
}

void App::DevSetState(int64_t id, const std::string& state) {
    if (!developer() || issues.state_ticket != 0) return;
    if (issues.detail_for == id) issues.detail.state = state;
    issues.state_ticket = client->DevSetIssueState(id, state);
}

void App::DevDeleteReport(int64_t id) {
    if (!developer() || issues.delete_ticket != 0) return;
    issues.delete_ticket = client->DevDeleteIssue(id);
}

std::filesystem::path App::DevIssueDir(const xlive::Client::Issue* is) const {
    std::filesystem::path root;
#ifdef _WIN32
    if (const std::string home = xlive::Env("USERPROFILE"); !home.empty()) root = home;
#else
    if (const std::string home = xlive::Env("HOME"); !home.empty()) root = home;
#endif
    if (root.empty()) root = std::filesystem::current_path();
    root = root / "XenonLive" / "Player Issues";
    if (!is) return root;
    // "#7 - Case Zero - pokisal": a name that reads on its own in a file
    // browser. Characters a filesystem refuses are swapped out.
    std::string game_name;
    for (const auto& t : config.titles) {
        if (t.title_id == is->title_id && !t.name.empty()) game_name = t.name;
    }
    if (game_name.empty()) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08x", is->title_id);
        game_name = buf;
    }
    std::string name = "#" + std::to_string(is->id) + " - " + game_name + " - " + is->gamertag;
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            c = '_';
        }
    }
    return root / name;
}

void App::DevDownload(const xlive::Client::Issue& is) {
    if (!developer() || issues.download_ticket != 0 || is.id == 0) return;
    const std::filesystem::path dir = DevIssueDir(&is);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        issues.download_error = "Cannot create " + dir.string();
        return;
    }
    // The words and the machine go in beside the files, so the folder is
    // the whole report without the launcher.
    {
        std::ofstream out(dir / "issue.txt", std::ios::binary | std::ios::trunc);
        out << "Issue #" << is.id << "\n"
            << "Game: " << dir.filename().string() << " " << is.game_version << "\n"
            << "Reporter: " << is.gamertag << "\n"
            << "State: " << is.state << "\n"
            << "Filed: " << is.created_at << "\n"
            << "Captured: " << is.captured_at << "\n\n"
            << "TITLE: " << is.title << "\n\nSUMMARY:\n" << is.summary << "\n\nSTEPS:\n" << is.steps
            << "\n\nSYSTEM:\n" << is.system_json << "\n";
    }
    issues.download_error.clear();
    issues.download_queue.clear();
    for (const auto& f : is.files) issues.download_queue.push_back(f.name);
    if (issues.download_queue.empty()) return;
    issues.downloading = is.id;
    const std::string first = issues.download_queue.front();
    issues.download_queue.erase(issues.download_queue.begin());
    issues.download_ticket = client->DevSaveIssueFile(is.id, first, (dir / first).string());
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

namespace {

// A catalog key, or the launcher's own.
const CatalogGame* EntryByKey(const std::string& key) {
    if (key == LauncherSelf().key) return &LauncherSelf();
    return CatalogByKey(key);
}

// "v1.1.0" and "1.1.0" are the same version.
std::string BareVersion(std::string tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    return tag;
}

}  // namespace

void App::CheckReleases() {
    for (const CatalogGame& item : Catalog()) QueueInstallJob(item.key, false);
    // A dev build has nothing to compare against and stays out of it.
    if (std::string(LauncherVersion()) != "dev") QueueInstallJob(LauncherSelf().key, false);
    last_release_check = ImGui::GetTime();
    next_release_check_ = last_release_check + kReleaseCheckInterval;
}

bool App::launcher_updating() const {
    if (installer.busy() && current_job_.install && current_job_.key == LauncherSelf().key) return true;
    for (const InstallJob& job : install_queue_) {
        if (job.install && job.key == LauncherSelf().key) return true;
    }
    return false;
}

void App::UpdateLauncher() {
    std::string why;
    if (!SelfUpdatePossible(why)) {
        launcher_update_error = why;
        return;
    }
    if (game.running()) {
        launcher_update_error = "a game is running; quit it first, the launcher restarts to update";
        return;
    }
    launcher_update_error.clear();
    QueueInstallJob(LauncherSelf().key, true);
}

bool App::release_check_running() const {
    if (installer.busy() && !current_job_.install) return true;
    for (const InstallJob& job : install_queue_) {
        if (!job.install) return true;
    }
    return false;
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
        const CatalogGame* item = EntryByKey(progress.key);
        std::fprintf(stderr, "[installer] %s: %s %s\n", progress.key.c_str(),
                     PhaseName(progress.phase), progress.message.c_str());
        if (progress.key == LauncherSelf().key) {
            if (progress.phase == InstallPhase::Failed && current_job_.install) {
                launcher_update_error = progress.message;
                toasts.Push("Launcher update failed: " + progress.message, 8.0);
            } else if (progress.phase == InstallPhase::Failed) {
                // The launcher's own repository may not answer (private, no
                // release yet): the log knows, the player is not bothered.
                std::fprintf(stderr, "[launcher] no launcher release to compare against: %s\n",
                             progress.message.c_str());
            } else if (progress.installed) {
                // Downloaded and verified beside us: swap and go.
                std::string error;
                if (ApplySelfUpdate(SelfUpdateStagingDir(), error)) {
                    std::fprintf(stderr, "[launcher] updated to %s; restarting\n",
                                 progress.release.tag.c_str());
                    quit = true;
                } else {
                    launcher_update_error = error;
                    toasts.Push("Launcher update failed: " + error, 8.0);
                }
            } else {
                release_check_error.clear();
                release_pages[progress.key] = progress.release.html_url;
                const bool newer = BareVersion(progress.release.tag) != LauncherVersion();
                launcher_update = newer ? progress.release.tag : std::string();
                if (newer && announced_updates_.insert("launcher@" + progress.release.tag).second) {
                    toasts.Push("XenonLive Launcher " + progress.release.tag +
                                    " is available - update from Home",
                                8.0);
                }
            }
            installer.Acknowledge();
            return;
        }
        if (progress.phase == InstallPhase::Failed && !current_job_.install) {
            // A background check that could not reach GitHub is a line on
            // the Home tab, not a toast every five minutes.
            release_check_error = progress.message;
        } else if (progress.phase == InstallPhase::Failed) {
            install_errors[progress.key] = progress.message;
            if (item) toasts.Push(std::string(item->name) + ": " + progress.message, 8.0);
        } else if (item) {
            release_check_error.clear();
            latest_tags[progress.key] = progress.release.tag;
            release_pages[progress.key] = progress.release.html_url;
            // A newer release than the installed one is said once.
            const int installed_at = TitleIndexForKey(item->key);
            if (installed_at >= 0 && !progress.installed &&
                config.titles[size_t(installed_at)].version != progress.release.tag &&
                announced_updates_.insert(std::string(item->key) + "@" + progress.release.tag).second) {
                toasts.Push(std::string(item->name) + " " + progress.release.tag +
                                " is available - update from Home",
                            8.0);
            }
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
        current_job_ = job;
        if (const CatalogGame* item = EntryByKey(job.key)) {
            if (job.install && job.key == LauncherSelf().key) {
                installer.Install(*item, SelfUpdateStagingDir());
            } else if (job.install) {
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
                presence_news_.Reset();
                friends.mine = xlive::Client::Presence{};
                friends.mine_loaded = false;
                achievements.loaded = false;
                if (tab == Tab::Profile) tab = Tab::Friends;
                profile = ProfileState{};
                messages = MessagesState{};
                issues = IssuesState{};
                account = AccountState{};
                if (tab == Tab::Account) tab = Tab::Home;
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
                }
            }
            for (const auto& line : presence_news_.Update(now, ImGui::GetTime())) {
                toasts.Push(line.gamertag + " " + line.text);
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
    // Releases: on the first frame, then every five minutes.
    if (ImGui::GetTime() >= next_release_check_) CheckReleases();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // No padding of its own: the rail and the content each bring theirs,
    // and the painted backgrounds reach the window's edge.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("XenonLive", nullptr, flags);
    ImGui::PopStyleVar(3);

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
    // The pad's shoulder buttons step the rail; B leaves a page that has a
    // parent (a friend's profile, the account screen) — unless a text box
    // has the focus, where B is its cancel.
    if (!show_signin) {
        static const Tab kRail[] = {Tab::Home, Tab::Friends, Tab::Messages, Tab::Invites,
                                    Tab::Achievements, Tab::Issues, Tab::Support};
        const int n = int(sizeof(kRail) / sizeof(kRail[0]));
        int at = 0;
        for (int i = 0; i < n; ++i) {
            if (kRail[i] == tab || (tab == Tab::Profile && kRail[i] == Tab::Friends)) at = i;
        }
        int step = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) step = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) step = 1;
        if (step != 0) {
            tab = kRail[(at + step + n) % n];
            if (tab == Tab::Issues) {
                RescanCaptures();
                if (!issues.searched && issues.search_ticket == 0) SearchIssues();
            }
            if (tab == Tab::Achievements && !achievements.loaded && achievements.ticket == 0 &&
                !config.titles.empty()) {
                LoadAchievements(achievements.title_index < 0 ? 0 : achievements.title_index);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) && !ImGui::GetIO().WantTextInput) {
            if (tab == Tab::Profile) tab = Tab::Friends;
            else if (tab == Tab::Account) tab = Tab::Home;
        }
    }
    if (show_signin) {
        // The whole window is the content look: the gradient and the glow.
        xlive::theme::ContentBackground(viewport->WorkPos,
                                        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x,
                                               viewport->WorkPos.y + viewport->WorkSize.y));
        DrawSignIn(*this);
    } else {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 end(viewport->WorkPos.x + viewport->WorkSize.x,
                         viewport->WorkPos.y + viewport->WorkSize.y);
        xlive::theme::RailBackground(origin, ImVec2(origin.x + kRailWidth * ui_scale, end.y));
        xlive::theme::ContentBackground(ImVec2(origin.x + kRailWidth * ui_scale, origin.y), end);
        DrawRail();
        ImGui::SameLine(0.0f, 0.0f);
        DrawContent();
    }
    ImGui::End();

    toasts.Draw();
}

void App::DrawRail() {
    namespace theme = xlive::theme;
    // The background was painted by Frame; the child is see-through.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 16.0f));
    ImGui::BeginChild("rail", ImVec2(kRailWidth * ui_scale, 0.0f), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    // The wordmark, lime on charcoal, the way the dashboard's was.
    ImGui::PushFont(fonts.title);
    ImGui::TextColored(theme::kLime, "Xenon");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted("Live");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fonts.title->FontSize - ImGui::GetTextLineHeight() - 2.0f);
    ImGui::TextColored(launcher_update.empty() ? theme::kMuted : theme::kAmber, "%s%s",
                       LauncherVersion(), launcher_update.empty() ? "" : " *");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    const auto blade = [&](const char* label, Tab which, theme::Icon icon,
                           const char* badge = nullptr) {
        const bool selected = tab == which || (which == Tab::Friends && tab == Tab::Profile);
        if (theme::RailItem(label, selected, badge, icon)) {
            tab = which;
            if (which == Tab::Achievements && !achievements.loaded && achievements.ticket == 0 &&
                !config.titles.empty()) {
                LoadAchievements(achievements.title_index < 0 ? 0 : achievements.title_index);
            }
            if (which == Tab::Issues) {
                RescanCaptures();
                if (!issues.searched && issues.search_ticket == 0) SearchIssues();
            }
        }
    };
    blade("Home", Tab::Home, theme::Icon::Home);
    blade("Friends", Tab::Friends, theme::Icon::Friends);
    char unread[16] = {};
    if (unread_messages() > 0) std::snprintf(unread, sizeof(unread), "%d", unread_messages());
    blade("Messages", Tab::Messages, theme::Icon::Messages, unread);
    const size_t inbox = client ? client->invites().size() : 0;
    char badge[16] = {};
    if (inbox > 0) std::snprintf(badge, sizeof(badge), "%zu", inbox);
    blade("Invites", Tab::Invites, theme::Icon::Invites, badge);
    blade("Achievements", Tab::Achievements, theme::Icon::Achievements);
    // Captures waiting for a decision. Scanned once at start and whenever
    // the tab opens; a port writing one while the launcher sits open is
    // seen on the next open or Rescan.
    if (!issues.scanned) RescanCaptures();
    char waiting[16] = {};
    if (!issues.captures.empty()) std::snprintf(waiting, sizeof(waiting), "%zu", issues.captures.size());
    blade("Issues", Tab::Issues, theme::Icon::Issues, waiting);
    blade("Support", Tab::Support, theme::Icon::Support);

    // The gamercard, at the bottom.
    const float card_h = fonts.heading->FontSize + ImGui::GetTextLineHeight() * 2.0f + 26.0f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - card_h - 14.0f);
    const bool online = client && client->online();
    if (theme::Gamercard(gamertag().c_str(), client ? client->identity().gamerscore : 0u, online,
                         online ? "online" : "offline") &&
        signed_in()) {
        OpenAccount();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::DrawContent() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
    ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened,
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
        case Tab::Issues:       DrawIssues(*this); break;
        case Tab::Support:      DrawSupport(*this); break;
        case Tab::Profile:      DrawProfile(*this); break;
        case Tab::Account:      DrawAccount(*this); break;
    }
    ImGui::EndChild();
}

}  // namespace launcher
