// The friends list: who is online and where, requests in and out, and the
// buttons that change it. friends() is a cache read, so it is called every
// frame; every write is a ticket the App collects.
#include <cstring>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

using Friend = xlive::Client::Friend;
using Relation = xlive::Client::Relation;
using PresenceState = xlive::Client::PresenceState;

}  // namespace

std::string PresenceLine(const xlive::Client::Presence& p) {
    switch (p.state) {
        case PresenceState::Offline: return "offline";
        case PresenceState::Online:  return "online";
        case PresenceState::Playing: {
            std::string line = "playing " + (p.title_name.empty() ? "a game" : p.title_name);
            if (!p.rich_text.empty() && p.rich_text != p.title_name) line += " - " + p.rich_text;
            if (p.joinable) line += " (joinable)";
            return line;
        }
    }
    return "";
}

namespace {

void DrawFriendRow(App& app, const Friend& f, bool can_invite) {
    ImGui::PushID(int(f.xuid & 0x7FFFFFFF));
    ImGui::PushID(int(f.xuid >> 32));
    ImGui::BeginChild("row", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

    const bool online = f.presence.online();
    // The status dot, then the name in the heading face: the name is the
    // way in to their profile page.
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float h = app.fonts.heading->FontSize;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(pos.x + 5.0f, pos.y + h * 0.55f), 4.5f,
            ImGui::GetColorU32(online ? xlive::theme::kLime : xlive::theme::kMuted));
        ImGui::Dummy(ImVec2(12.0f, h));
        ImGui::SameLine();
    }
    ImGui::PushFont(app.fonts.heading);
    if (ImGui::Selectable(f.gamertag.c_str(), false, ImGuiSelectableFlags_None,
                          ImVec2(ImGui::CalcTextSize(f.gamertag.c_str()).x + 4.0f, 0.0f))) {
        app.OpenProfile(f.xuid, f.gamertag);
    }
    ImGui::PopFont();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("View profile");
    ImGui::SameLine();
    ImGui::TextDisabled("%u G", f.gamerscore);
    ImGui::SameLine();
    if (ImGui::SmallButton("Profile")) app.OpenProfile(f.xuid, f.gamertag);

    switch (f.relation) {
        case Relation::Friend: {
            ImGui::TextDisabled("%s", PresenceLine(f.presence).c_str());
            ImGui::BeginDisabled(!can_invite || !online);
            if (ImGui::SmallButton("Invite")) {
                app.Issue(app.client->SendInvite(app.friends.mine.session_id, f.xuid),
                          "Invite " + f.gamertag);
                app.toasts.Push("Invitation sent to " + f.gamertag);
            }
            ImGui::EndDisabled();
            if (!can_invite && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Start a game session first: an invitation names the "
                                  "session you are in.");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                app.Issue(app.client->RemoveFriend(f.xuid), "Remove " + f.gamertag);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Block")) {
                app.Issue(app.client->BlockPlayer(f.xuid), "Block " + f.gamertag);
            }
            break;
        }
        case Relation::RequestReceived:
            ImGui::TextDisabled("wants to be your friend");
            // Asking back is accepting: there is one route on the server.
            if (ImGui::SmallButton("Accept")) {
                app.Issue(app.client->AddFriend(f.xuid), "Accept " + f.gamertag);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Decline")) {
                app.Issue(app.client->RemoveFriend(f.xuid), "Decline " + f.gamertag);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Block")) {
                app.Issue(app.client->BlockPlayer(f.xuid), "Block " + f.gamertag);
            }
            break;
        case Relation::RequestSent:
            ImGui::TextDisabled("request sent");
            if (ImGui::SmallButton("Withdraw")) {
                app.Issue(app.client->RemoveFriend(f.xuid), "Withdraw " + f.gamertag);
            }
            break;
        default:
            break;
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopID();
}

}  // namespace

void DrawFriends(App& app) {
    // Read my own presence once per opening of the tab, not per frame.
    if (!app.friends.open) {
        app.friends.open = true;
        app.RefreshMyPresence();
    }
    // -- add by gamertag ------------------------------------------------------
    ImGui::SetNextItemWidth(220.0f);
    const bool enter = ImGui::InputTextWithHint("##add", "gamertag", app.friends.add_gamertag,
                                                sizeof(app.friends.add_gamertag),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add friend") || enter) && app.friends.add_gamertag[0] != '\0') {
        const std::string tag = app.friends.add_gamertag;
        app.Issue(app.client->AddFriendByGamertag(tag), "Add " + tag);
        std::memset(app.friends.add_gamertag, 0, sizeof(app.friends.add_gamertag));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        app.Issue(app.client->RefreshFriends(), "Refresh");
        app.friends.mine_loaded = false;
        app.RefreshMyPresence();
    }

    // -- what I can invite into ----------------------------------------------
    const bool can_invite = app.friends.mine_loaded && app.friends.mine.session_id != 0;
    if (app.friends.presence_ticket != 0) {
        ImGui::TextDisabled("reading your presence...");
    } else if (can_invite) {
        ImGui::TextDisabled("in a session of %s - friends can be invited",
                            app.friends.mine.title_name.c_str());
    } else {
        ImGui::TextDisabled("not in a game session - Invite needs one");
    }

    // -- the list -----------------------------------------------------------
    const auto list = app.client->friends();
    std::vector<const Friend*> friends, received, sent;
    for (const Friend& f : list) {
        switch (f.relation) {
            case Relation::Friend:          friends.push_back(&f); break;
            case Relation::RequestReceived: received.push_back(&f); break;
            case Relation::RequestSent:     sent.push_back(&f); break;
            default: break;
        }
    }

    if (!received.empty()) {
        xlive::theme::Section("Requests");
        for (const Friend* f : received) DrawFriendRow(app, *f, can_invite);
    }

    xlive::theme::Section("Friends");
    if (friends.empty()) {
        ImGui::TextDisabled(list.empty() && !app.client->gateway_connected()
                                ? "No friends yet, and no connection to the server."
                                : "No friends yet.");
    }
    // Online first; the cache is sorted by gamertag within that.
    for (const Friend* f : friends) if (f->presence.online()) DrawFriendRow(app, *f, can_invite);
    for (const Friend* f : friends) if (!f->presence.online()) DrawFriendRow(app, *f, can_invite);

    if (!sent.empty()) {
        xlive::theme::Section("Sent");
        for (const Friend* f : sent) DrawFriendRow(app, *f, can_invite);
    }
}

}  // namespace launcher
