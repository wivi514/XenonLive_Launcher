// Sign in / register. Remembers nothing but the server URL; the library
// keeps the tokens.
#include <cstdio>
#include <cstring>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void DrawSignIn(App& app) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = 420.0f;
    ImGui::SetCursorPosX((avail.x - width) * 0.5f);
    // The taller forms start higher so they fit a small window whole.
    const bool tall = app.signin.mode != App::SignInState::Mode::SignIn;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * (tall ? 0.05f : 0.18f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
    ImGui::BeginChild("signin", ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PushFont(app.fonts.title);
    ImGui::TextColored(xlive::theme::kLime, "Xenon");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted("Live");
    ImGui::PopFont();
    if (app.adding_account) {
        ImGui::SameLine(width - 80.0f);
        if (ImGui::SmallButton("Back")) app.adding_account = false;
    }
    ImGui::Separator();
    ImGui::Spacing();

    const bool busy = app.signin.ticket != 0;

    // -- saved accounts --------------------------------------------------------
    // One row per account this machine has signed into. Use swaps its saved
    // tokens in without a password; an account that was signed out keeps
    // its name here and needs the password again.
    const uint64_t current = app.signed_in() ? app.client->identity().xuid : 0;
    bool any = false;
    for (const SavedAccount& account : app.accounts.list()) {
        if (account.xuid == current) continue;
        any = true;
    }
    if (any && app.signin.mode != App::SignInState::Mode::SignIn) any = false;
    if (any) {
        ImGui::TextUnformatted("Saved accounts");
        uint64_t forget = 0;
        for (const SavedAccount& account : app.accounts.list()) {
            if (account.xuid == current) continue;
            ImGui::PushID(int(account.xuid & 0x7FFFFFFF));
            ImGui::PushID(int(account.xuid >> 32));
            ImGui::BeginChild("acct", ImVec2(0.0f, 0.0f),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(account.gamertag.c_str());
            ImGui::SameLine(width - 150.0f);
            ImGui::BeginDisabled(busy);
            if (account.has_tokens()) {
                if (ImGui::SmallButton("Use")) {
                    std::string error;
                    if (!app.SwitchAccount(account.xuid, error)) app.toasts.Push(error, 6.0);
                }
            } else {
                if (ImGui::SmallButton("Password")) {
                    std::snprintf(app.signin.gamertag, sizeof(app.signin.gamertag), "%s",
                                  account.gamertag.c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Forget")) forget = account.xuid;
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::PopID();
            ImGui::PopID();
        }
        if (forget != 0) app.ForgetAccount(forget);
        ImGui::Spacing();
    }
    {
        const char* heading = nullptr;
        switch (app.signin.mode) {
        case App::SignInState::Mode::SignIn:
            heading = app.adding_account ? "Add another account" : (any ? "Or sign in" : nullptr);
            break;
        case App::SignInState::Mode::Register: heading = "Create an account"; break;
        case App::SignInState::Mode::Forgot: heading = "Forgot your password"; break;
        }
        if (heading) {
            ImGui::PushFont(app.fonts.heading);
            ImGui::TextColored(xlive::theme::kLime, "%s", heading);
            ImGui::PopFont();
            ImGui::Spacing();
        }
    }
    using Mode = App::SignInState::Mode;
    using Pending = App::SignInState::Pending;
    auto& st = app.signin;
    const auto switch_to = [&](Mode mode) {
        st.mode = mode;
        st.error.clear();
        st.sent_to.clear();
        std::memset(st.code, 0, sizeof(st.code));
    };
    const auto start = [&](Pending what, xlive::Client::Ticket ticket) {
        st.pending = what;
        st.error.clear();
        st.ticket = ticket;
    };
    // Sign in, register and reset all run against the server in the box
    // below; a change there takes effect on submit.
    const auto use_server = [&] {
        if (std::string(st.server) != app.config.server) app.RestartClient();
    };
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::BeginDisabled(busy);
    switch (st.mode) {
    case Mode::SignIn: {
        ImGui::TextUnformatted("Gamertag");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##gamertag", st.gamertag, sizeof(st.gamertag));
        ImGui::TextUnformatted("Password");
        ImGui::SetNextItemWidth(-1.0f);
        const bool enter = ImGui::InputText("##password", st.password, sizeof(st.password),
                                            ImGuiInputTextFlags_Password |
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        if ((ImGui::Button("Sign in", ImVec2(-1.0f, 0.0f)) || enter) && app.client) {
            use_server();
            start(Pending::SignIn, app.client->SignIn(st.gamertag, st.password));
        }
        ImGui::Spacing();
        if (ImGui::TextLink("Create an account")) switch_to(Mode::Register);
        ImGui::SameLine(0.0f, 24.0f);
        if (ImGui::TextLink("Forgot your password?")) switch_to(Mode::Forgot);
        break;
    }
    case Mode::Register: {
        ImGui::TextUnformatted("Gamertag");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##gamertag", st.gamertag, sizeof(st.gamertag));
        ImGui::TextUnformatted("Password");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##password", st.password, sizeof(st.password),
                         ImGuiInputTextFlags_Password);
        ImGui::TextUnformatted("Email");
        ImGui::SameLine();
        ImGui::TextColored(xlive::theme::kMuted, "optional");
        ImGui::SetNextItemWidth(-1.0f);
        const bool enter = ImGui::InputText("##email", st.email, sizeof(st.email),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        // The deal, stated where the choice is made: the address exists to
        // get back in, and for nothing else.
        ImGui::PushStyleColor(ImGuiCol_Text, xlive::theme::kMuted);
        ImGui::TextWrapped("Only used to get back in if you forget your password. Nothing else "
                           "is ever sent to it: no news, no updates, nothing. It is never shown "
                           "to other players.");
        ImGui::TextWrapped("Without one, a forgotten password means the account is lost.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if ((ImGui::Button("Create account", ImVec2(-1.0f, 0.0f)) || enter) && app.client) {
            use_server();
            start(Pending::Register, app.client->Register(st.gamertag, st.password, st.email));
        }
        ImGui::Spacing();
        if (ImGui::TextLink("Already have an account? Sign in")) switch_to(Mode::SignIn);
        break;
    }
    case Mode::Forgot: {
        if (st.sent_to.empty()) {
            ImGui::TextUnformatted("Gamertag");
            ImGui::SetNextItemWidth(-1.0f);
            const bool enter = ImGui::InputText("##gamertag", st.gamertag, sizeof(st.gamertag),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PushStyleColor(ImGuiCol_Text, xlive::theme::kMuted);
            ImGui::TextWrapped("A code goes to the email you gave when you created the account. "
                               "An account created without one cannot be recovered.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if ((ImGui::Button("Send me a code", ImVec2(-1.0f, 0.0f)) || enter) && app.client) {
                use_server();
                start(Pending::Forgot, app.client->ForgotPassword(st.gamertag));
            }
        } else {
            ImGui::TextWrapped("A code was sent to %s. It works for 30 minutes.", st.sent_to.c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted("Code from the email");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##code", st.code, sizeof(st.code),
                             ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
            ImGui::TextUnformatted("New password");
            ImGui::SetNextItemWidth(-1.0f);
            const bool enter = ImGui::InputText("##password", st.password, sizeof(st.password),
                                                ImGuiInputTextFlags_Password |
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            if ((ImGui::Button("Set new password", ImVec2(half, 0.0f)) || enter) && app.client) {
                start(Pending::Reset,
                      app.client->ResetPassword(st.gamertag, st.code, st.password));
            }
            ImGui::SameLine();
            if (ImGui::Button("Send another code", ImVec2(-1.0f, 0.0f)) && app.client) {
                std::memset(st.code, 0, sizeof(st.code));
                start(Pending::Forgot, app.client->ForgotPassword(st.gamertag));
            }
        }
        ImGui::Spacing();
        if (ImGui::TextLink("Back to sign in")) switch_to(Mode::SignIn);
        break;
    }
    }
    ImGui::EndDisabled();

    // The server, for a self-hoster or a developer. Everyone else never sees
    // it: the default is the XenonLive server and the games follow it.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Advanced: server")) {
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##server", app.signin.server, sizeof(app.signin.server));
        if (app.client && std::string(app.signin.server) != app.config.server) {
            if (ImGui::SmallButton("Use this server")) app.RestartClient();
            ImGui::SameLine();
        }
        if (std::string(app.signin.server) != kDefaultServer) {
            if (ImGui::SmallButton("Back to the default")) {
                std::snprintf(app.signin.server, sizeof(app.signin.server), "%s", kDefaultServer);
                if (app.config.server != kDefaultServer) app.RestartClient();
            }
        }
        if (app.config.allow_insecure) {
            ImGui::TextColored(xlive::theme::kAmber,
                               "allow_insecure is on in launcher.json: plain http, no certificate check");
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (busy) {
        const char* doing = "Signing in...";
        switch (st.pending) {
        case Pending::SignIn: break;
        case Pending::Register: doing = "Creating the account..."; break;
        case Pending::Forgot: doing = "Sending the code..."; break;
        case Pending::Reset: doing = "Setting the password..."; break;
        }
        ImGui::TextDisabled("%s", doing);
    } else if (!st.error.empty()) {
        // The server's code, verbatim, then a line a player can act on. A
        // bug report can quote the code.
        ImGui::TextColored(xlive::theme::kRed, "%s", st.error.c_str());
        const std::string& e = st.error;
        const char* help = nullptr;
        if (e == "bad_gamertag") {
            help = "A gamertag starts with a letter and is at most 15 letters, digits and "
                   "single spaces.";
        } else if (e == "bad_password") {
            help = "A password is at least 8 characters.";
        } else if (e == "bad_email") {
            help = "That does not look like an email address. Leave it empty if you would "
                   "rather not give one.";
        } else if (e == "taken") {
            help = "That gamertag (or email) already has an account.";
        } else if (e == "no_account") {
            help = "No account has that gamertag.";
        } else if (e == "no_email") {
            help = "This account was created without an email, so there is no way to "
                   "send it a code. It cannot be recovered.";
        } else if (e == "mail_unavailable") {
            help = "This server cannot send email, so it cannot recover accounts.";
        } else if (e == "mail_failed") {
            help = "The server could not send the mail just now. Try again in a minute.";
        } else if (e == "bad_code") {
            help = "That code is wrong, expired or already used. Check the newest mail, or "
                   "send another code.";
        } else if (e == "rate_limited") {
            help = "Too many tries from this address. Wait a while.";
        } else if (e == "no_server") {
            help = "No server is configured: open Advanced and press Back to the default.";
        }
        if (help) ImGui::TextWrapped("%s", help);
    } else if (app.client) {
        // What the library is doing with a saved session, if there is one:
        // "connecting", "offline: retrying in 4s". A player with a session
        // and a server that is down sees why the home screen is not here.
        ImGui::TextDisabled("%s", app.client->status().c_str());
    }
    ImGui::EndChild();
}

}  // namespace launcher
