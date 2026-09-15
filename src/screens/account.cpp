// The account screen: the recovery email, the country, the password. Three
// small forms, each with its own ticket, on one page. What cannot be
// changed is not here — the gamertag is the account, and there is no
// gamerpic because the server keeps none.
#include <cstdio>
#include <cstring>
#include <string>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

using xlive::theme::kLime;
using xlive::theme::kMuted;
using xlive::theme::kPanelHi;
using xlive::theme::kRed;

// The server's code, then a line a player can act on.
void Problem(const std::string& code) {
    if (code.empty()) return;
    ImGui::TextColored(kRed, "%s", code.c_str());
    const char* help = nullptr;
    if (code == "bad_email") help = T("That does not look like an email address.");
    else if (code == "taken") help = T("Another account already uses that address.");
    else if (code == "bad_country") help = T("Two letters: CA, US, FR, JP...");
    else if (code == "bad_credentials") help = T("The current password is wrong.");
    else if (code == "bad_password") help = T("A password is at least 8 characters.");
    else if (code == "mismatch") help = T("The two new passwords differ.");
    else if (code == "signed_out" || code == "unreachable") help = T("The server could not be reached.");
    if (help) ImGui::TextWrapped("%s", help);
}

}  // namespace

void DrawAccount(App& app) {
    auto& st = app.account;
    const xlive::Identity id = app.client->identity();

    {
        char sub[64];
        std::snprintf(sub, sizeof(sub), T("%u G   your account"), id.gamerscore);
        if (xlive::theme::PageHeader(id.gamertag.c_str(), sub, T("Sign out"))) app.SignOut();
    }
    ImGui::Spacing();

    const float box = 360.0f;

    // -- recovery email ----------------------------------------------------
    xlive::theme::Section(T("Recovery email"));
    if (!st.email_filled) {
        std::snprintf(st.email, sizeof(st.email), "%s", id.email.c_str());
        st.email_filled = true;
    }
    if (id.email.empty()) {
        ImGui::TextColored(xlive::theme::kAmber, T("None. If you forget your password, this account cannot be recovered."));
    } else {
        ImGui::Text(T("A code can be sent to %s if you forget your password."), id.email.c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(T("Only for getting back in. Nothing else is ever sent to it - no news, no updates - and it is never shown to other players."));
    ImGui::PopStyleColor();
    {
        const bool busy = st.email_ticket != 0;
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(box);
        const bool enter = ImGui::InputTextWithHint("##email", T("you@example.org"), st.email,
                                                    sizeof(st.email),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool changed = std::string(st.email) != id.email;
        ImGui::BeginDisabled(!changed || st.email[0] == '\0');
        if (ImGui::Button(T("Save##email")) || (enter && changed && st.email[0])) app.SaveEmail(false);
        ImGui::EndDisabled();
        if (!id.email.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRed);
            if (ImGui::Button(T("Remove##email"))) app.SaveEmail(true);
            ImGui::PopStyleColor(2);
        }
        ImGui::EndDisabled();
        if (busy) ImGui::TextDisabled(T("Saving..."));
        Problem(st.email_error);
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // -- country -----------------------------------------------------------
    xlive::theme::Section(T("Country"));
    if (!st.country_filled) {
        std::snprintf(st.country, sizeof(st.country), "%s", id.country.c_str());
        st.country_filled = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(T("Two letters, shown on your gamercard to friends. Optional."));
    ImGui::PopStyleColor();
    {
        const bool busy = st.country_ticket != 0;
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(60.0f);
        const bool enter = ImGui::InputTextWithHint("##country", T("CA"), st.country, sizeof(st.country),
                                                    ImGuiInputTextFlags_CharsUppercase |
                                                        ImGuiInputTextFlags_CharsNoBlank |
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool changed = std::string(st.country) != id.country;
        ImGui::BeginDisabled(!changed || st.country[0] == '\0');
        if (ImGui::Button(T("Save##country")) || (enter && changed && st.country[0])) app.SaveCountry(false);
        ImGui::EndDisabled();
        if (!id.country.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRed);
            if (ImGui::Button(T("Remove##country"))) app.SaveCountry(true);
            ImGui::PopStyleColor(2);
        }
        ImGui::EndDisabled();
        if (busy) ImGui::TextDisabled(T("Saving..."));
        Problem(st.country_error);
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // -- password ----------------------------------------------------------
    xlive::theme::Section(T("Password"));
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(T("Changing it signs every other device out of this account. This one stays signed in."));
    ImGui::PopStyleColor();
    {
        const bool busy = st.password_ticket != 0;
        ImGui::BeginDisabled(busy);
        ImGui::TextUnformatted(T("Current password"));
        ImGui::SetNextItemWidth(box);
        ImGui::InputText("##current", st.current, sizeof(st.current), ImGuiInputTextFlags_Password);
        ImGui::TextUnformatted(T("New password"));
        ImGui::SetNextItemWidth(box);
        ImGui::InputText("##next", st.next, sizeof(st.next), ImGuiInputTextFlags_Password);
        ImGui::TextUnformatted(T("New password again"));
        ImGui::SetNextItemWidth(box);
        const bool enter = ImGui::InputText("##confirm", st.confirm, sizeof(st.confirm),
                                            ImGuiInputTextFlags_Password |
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        const bool ready = st.current[0] && st.next[0] && st.confirm[0];
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button(T("Change password")) || (enter && ready)) app.ChangePassword();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (busy) ImGui::TextDisabled(T("Changing..."));
        Problem(st.password_error);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // -- the launcher itself --------------------------------------------
    xlive::theme::Section(T("Launcher"));
    ImGui::TextUnformatted(T("Language"));
    ImGui::SameLine();
    DrawLanguagePicker(app, box);
}

}  // namespace launcher
