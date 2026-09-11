// Sign in / register. Remembers nothing but the server URL; the library
// keeps the tokens.
#include <cstring>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void DrawSignIn(App& app) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = 380.0f;
    ImGui::SetCursorPosX((avail.x - width) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.18f);

    ImGui::BeginChild("signin", ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("XenonLive");
    ImGui::Separator();
    ImGui::Spacing();

    const bool busy = app.signin.ticket != 0;
    ImGui::BeginDisabled(busy);

    ImGui::TextUnformatted("Server");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##server", app.signin.server, sizeof(app.signin.server));
    if (app.client && std::string(app.signin.server) != app.config.server) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Use")) app.RestartClient();
    }

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
    if (ImGui::Button("Sign in", ImVec2(width * 0.5f - 4.0f, 0.0f)) || enter) submit(false);
    ImGui::SameLine();
    if (ImGui::Button("Register", ImVec2(-1.0f, 0.0f))) submit(true);
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (busy) {
        ImGui::TextDisabled(app.signin.registering ? "Registering..." : "Signing in...");
    } else if (!app.signin.error.empty()) {
        // The server's code, verbatim: "bad_credentials", "taken",
        // "bad_gamertag". A player can read it and a bug report can quote it.
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", app.signin.error.c_str());
        if (app.signin.error == "bad_gamertag") {
            ImGui::TextWrapped("A gamertag starts with a letter and is at most 15 letters, "
                               "digits and single spaces.");
        } else if (app.signin.error == "bad_password") {
            ImGui::TextWrapped("A password is at least 8 characters.");
        } else if (app.signin.error == "no_server") {
            ImGui::TextWrapped("Enter a server URL and press Use.");
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
