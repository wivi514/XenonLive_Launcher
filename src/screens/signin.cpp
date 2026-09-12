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
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.18f);

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
        ImGui::TextUnformatted(app.adding_account ? "Add another account" : "Or sign in");
        ImGui::Spacing();
    } else if (app.adding_account) {
        ImGui::TextUnformatted("Add another account");
        ImGui::Spacing();
    }
    ImGui::BeginDisabled(busy);

    ImGui::TextUnformatted("Gamertag");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##gamertag", app.signin.gamertag, sizeof(app.signin.gamertag));

    ImGui::TextUnformatted("Password");
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = ImGui::InputText("##password", app.signin.password,
                                        sizeof(app.signin.password),
                                        ImGuiInputTextFlags_Password |
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    const auto submit = [&](bool registering) {
        if (!app.client) return;
        if (std::string(app.signin.server) != app.config.server) app.RestartClient();
        app.signin.registering = registering;
        app.signin.error.clear();
        app.signin.ticket = registering
                                ? app.client->Register(app.signin.gamertag, app.signin.password)
                                : app.client->SignIn(app.signin.gamertag, app.signin.password);
    };
    if (ImGui::Button("Sign in", ImVec2((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 0.0f)) || enter) submit(false);
    ImGui::SameLine();
    if (ImGui::Button("Register", ImVec2(-1.0f, 0.0f))) submit(true);
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
        ImGui::TextDisabled(app.signin.registering ? "Registering..." : "Signing in...");
    } else if (!app.signin.error.empty()) {
        // The server's code, verbatim: "bad_credentials", "taken",
        // "bad_gamertag". A player can read it and a bug report can quote it.
        ImGui::TextColored(xlive::theme::kRed, "%s", app.signin.error.c_str());
        if (app.signin.error == "bad_gamertag") {
            ImGui::TextWrapped("A gamertag starts with a letter and is at most 15 letters, "
                               "digits and single spaces.");
        } else if (app.signin.error == "bad_password") {
            ImGui::TextWrapped("A password is at least 8 characters.");
        } else if (app.signin.error == "no_server") {
            ImGui::TextWrapped("No server is configured: open Advanced and press Back to the default.");
        }
    } else if (app.client) {
        // What the library is doing with a saved session, if there is one:
        // "connecting", "offline: retrying in 4s". A player with a session
        // and a server that is down sees why the home screen is not here.
        ImGui::TextDisabled("%s", app.client->status().c_str());
    }
    ImGui::EndChild();
}

}  // namespace launcher
