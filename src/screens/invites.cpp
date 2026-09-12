// The inbox. Accept launches the title (the game's own libxlive finds the
// accepted invitation at sync); Decline says no.
#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void DrawInvites(App& app) {
    const auto inbox = app.client->invites();
    xlive::theme::PageHeader("Invitations", inbox.empty() ? "nothing waiting" : nullptr);
    if (inbox.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, xlive::theme::kMuted);
        ImGui::TextWrapped("When a friend invites you into their game it lands here, and as a "
                           "notification. Accept starts the game if it is not running.");
        ImGui::PopStyleColor();
        return;
    }
    for (const xlive::Client::Invite& invite : inbox) {
        ImGui::PushID(int(invite.id));
        ImGui::BeginChild("invite", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::Text("%s", invite.from_gamertag.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("invited you to %s", app.TitleName(invite.title_id).c_str());

        const TitleEntry* title = app.config.FindTitle(invite.title_id);
        if (app.game.running()) {
            ImGui::TextDisabled("accepting tells the running game; it joins from there");
        } else if (!title) {
            ImGui::TextDisabled("not configured here: accepting will not launch anything");
        } else {
            ImGui::TextDisabled("accepting launches %s", title->name.c_str());
        }

        if (ImGui::Button("Accept")) app.AcceptInvite(invite);
        ImGui::SameLine();
        if (xlive::theme::SecondaryButton("Decline")) app.DeclineInvite(invite.id);
        ImGui::EndChild();
        ImGui::PopID();
    }
}

}  // namespace launcher
