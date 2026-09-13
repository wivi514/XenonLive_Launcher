// A friend's profile page: the gamercard (who they are, how far they are in
// each title) and, per title, their achievements next to this player's own.
// The card is anyone's; the progress and the comparison are only there once
// they have accepted this player as a friend, which the server enforces and
// the card reports.
#include <cstring>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

using Achievement = xlive::Client::Achievement;
using Relation = xlive::Client::Relation;

// "2026-09-12T14:03:22Z" -> "2026-09-12". The server's timestamps are RFC
// 3339 and a card does not need the time of day.
std::string DateOf(const std::string& rfc3339) {
    return rfc3339.size() >= 10 ? rfc3339.substr(0, 10) : rfc3339;
}

const Achievement* FindAchievement(const xlive::Client::TitleInfo& title, uint16_t id) {
    for (const Achievement& a : title.achievements) if (a.id == id) return &a;
    return nullptr;
}

void DrawProgressBar(uint32_t have, uint32_t of, const char* overlay) {
    const float fraction = of ? float(have) / float(of) : 0.0f;
    ImGui::ProgressBar(fraction, ImVec2(260.0f, 0.0f), overlay);
}

// One side of a comparison row: a check with the date, or a dash.
void DrawSide(const char* who, const Achievement* a) {
    ImGui::TextDisabled("%s", who);
    ImGui::SameLine();
    if (a && a->unlocked) {
        ImGui::TextColored(xlive::theme::kLime, "unlocked");
        if (!a->unlocked_at.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", DateOf(a->unlocked_at).c_str());
        }
    } else {
        ImGui::TextDisabled("-");
    }
}

void DrawCompare(App& app) {
    App::ProfileState& p = app.profile;
    if (p.theirs_ticket != 0 || p.mine_ticket != 0) {
        ImGui::TextDisabled("loading...");
        return;
    }
    if (!p.compare_error.empty()) {
        ImGui::TextColored(xlive::theme::kRed, "%s", p.compare_error.c_str());
        if (p.compare_error == "not_friends") {
            ImGui::TextWrapped("%s has not accepted you as a friend yet.", p.gamertag.c_str());
        }
        return;
    }
    if (!p.compare_loaded) return;

    const xlive::Client::TitleInfo& theirs = p.theirs;
    const xlive::Client::TitleInfo& mine = p.mine;
    unsigned their_count = 0, my_count = 0;
    for (const Achievement& a : theirs.achievements) their_count += a.unlocked;
    for (const Achievement& a : mine.achievements) my_count += a.unlocked;

    // The scoreboard.
    if (const Image tile_image = app.images.Title(theirs.title_id); tile_image.texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(tile_image.texture), ImVec2(48.0f, 48.0f));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(app.fonts.heading);
    ImGui::Text("%s", theirs.name.c_str());
    ImGui::PopFont();
    ImGui::Text("%s: %u / %u G, %u of %zu", p.gamertag.c_str(), theirs.gamerscore,
                theirs.max_gamerscore, their_count, theirs.achievements.size());
    ImGui::Text("You: %u / %u G, %u of %zu", mine.gamerscore, mine.max_gamerscore, my_count,
                mine.achievements.size());
    ImGui::EndGroup();

    ImGui::SetNextItemWidth(260.0f);
    const char* filters[] = {"Every achievement", "Only what they have and you don't",
                             "Only what you have and they don't"};
    ImGui::Combo("##filter", &p.compare_filter, filters, 3);
    ImGui::Separator();

    // The definitions come with both reads and are the same; walk theirs and
    // look mine up by id, so an achievement the server added between the two
    // reads (an import mid-comparison) shows as locked for me rather than
    // misaligning the rows.
    const float tile = 48.0f;
    unsigned shown = 0;
    for (const Achievement& t : theirs.achievements) {
        const Achievement* m = FindAchievement(mine, t.id);
        const bool i_have = m && m->unlocked;
        if (p.compare_filter == 1 && !(t.unlocked && !i_have)) continue;
        if (p.compare_filter == 2 && !(i_have && !t.unlocked)) continue;
        ++shown;

        // A secret neither of us has unlocked stays secret. One that either
        // has is not: the name is on the other player's card.
        const bool secret = t.hidden && !t.unlocked && !i_have;
        ImGui::PushID(t.id);
        ImGui::BeginChild("cmp", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        const Image image = secret ? Image{} : app.images.Achievement(theirs.title_id, t.id);
        if (image.texture) {
            const bool anyone = t.unlocked || i_have;
            const ImVec4 tint = anyone ? ImVec4(1, 1, 1, 1) : ImVec4(0.55f, 0.55f, 0.55f, 0.8f);
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(image.texture), ImVec2(tile, tile),
                               ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
        } else {
            ImGui::Dummy(ImVec2(tile, tile));
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                ImGui::GetColorU32(ImGuiCol_Border));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", secret ? "Secret achievement" : t.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%u G", t.score);
        DrawSide(p.gamertag.c_str(), &t);
        DrawSide("You", m);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopID();
    }
    if (shown == 0) {
        ImGui::TextDisabled(p.compare_filter == 1 ? "Nothing they have that you don't."
                                                  : "Nothing you have that they don't.");
    }
}

}  // namespace

void DrawProfile(App& app) {
    App::ProfileState& p = app.profile;
    if (ImGui::TextLink("< Friends")) {
        app.tab = Tab::Friends;
        return;
    }

    // -- the card -------------------------------------------------------------
    xlive::Client::Friend entry;
    const bool listed = app.client && app.client->FriendByXUID(p.xuid, entry);
    const bool is_friend = listed && entry.is_friend();

    ImGui::BeginDisabled(p.card_ticket != 0);
    const bool refresh = xlive::theme::PageHeader(p.gamertag.c_str(), nullptr, "Refresh");
    ImGui::EndDisabled();
    if (refresh) {
        const uint32_t compare = p.compare_title;
        app.OpenProfile(p.xuid, p.gamertag);
        if (compare) app.LoadCompare(compare);
    }
    if (p.card_ticket != 0) {
        ImGui::TextDisabled("loading...");
    } else if (!p.card_error.empty()) {
        ImGui::TextColored(xlive::theme::kRed, "%s", p.card_error.c_str());
    } else if (p.card_loaded) {
        const xlive::Client::Gamercard& card = p.card.card;
        ImGui::TextDisabled("%u G", card.gamerscore);
        if (!card.country.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", card.country.c_str());
        }
        if (!card.created_at.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- member since %s", DateOf(card.created_at).c_str());
        }
    }
    if (is_friend) ImGui::TextDisabled("%s", PresenceLine(entry.presence).c_str());

    // The same buttons the friends row has, so a page is not a dead end.
    if (app.client) {
        if (is_friend) {
            const bool can_invite = app.friends.mine_loaded && app.friends.mine.session_id != 0;
            ImGui::BeginDisabled(!can_invite || !entry.presence.online());
            if (ImGui::SmallButton("Invite")) {
                app.Issue(app.client->SendInvite(app.friends.mine.session_id, entry.xuid),
                          "Invite " + entry.gamertag);
                app.toasts.Push("Invitation sent to " + entry.gamertag);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Message")) app.OpenConversation(entry.xuid, entry.gamertag);
            ImGui::SameLine();
            if (xlive::theme::SmallSecondaryButton("Remove friend")) {
                app.Issue(app.client->RemoveFriend(entry.xuid), "Remove " + entry.gamertag);
            }
        } else if (listed && entry.relation == Relation::RequestReceived) {
            if (ImGui::SmallButton("Accept request")) {
                app.Issue(app.client->AddFriend(entry.xuid), "Accept " + entry.gamertag);
            }
            ImGui::SameLine();
            if (xlive::theme::SmallSecondaryButton("Decline")) {
                app.Issue(app.client->RemoveFriend(entry.xuid), "Decline " + entry.gamertag);
            }
        } else if (listed && entry.relation == Relation::RequestSent) {
            ImGui::TextDisabled("friend request sent");
            ImGui::SameLine();
            if (xlive::theme::SmallSecondaryButton("Withdraw")) {
                app.Issue(app.client->RemoveFriend(entry.xuid), "Withdraw " + entry.gamertag);
            }
        } else if (p.xuid != app.client->identity().xuid) {
            if (ImGui::SmallButton("Add friend")) {
                app.Issue(app.client->AddFriend(p.xuid), "Add " + p.gamertag);
            }
        }
    }
    ImGui::Separator();

    if (!p.card_loaded) return;
    const xlive::Client::Gamercard& card = p.card.card;

    // -- games ------------------------------------------------------------------
    xlive::theme::Section("Games");
    if (!card.achievements_visible) {
        ImGui::TextWrapped("%s's achievements are visible to the friends they have accepted.",
                           p.gamertag.c_str());
        return;
    }
    if (card.titles.empty()) ImGui::TextDisabled("This server has no titles imported.");
    for (const xlive::Client::PlayedTitle& t : card.titles) {
        ImGui::PushID(int(t.title_id));
        ImGui::BeginChild("game", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        if (const Image tile_image = app.images.Title(t.title_id); tile_image.texture) {
            ImGui::Image(reinterpret_cast<ImTextureID>(tile_image.texture), ImVec2(40.0f, 40.0f));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::PushFont(app.fonts.heading);
        ImGui::Text("%s", t.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("%u / %u G", t.gamerscore, t.max_gamerscore);
        if (!t.last_unlocked_at.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- last unlock %s", DateOf(t.last_unlocked_at).c_str());
        }
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%u of %u", t.unlocked, t.total);
        DrawProgressBar(t.unlocked, t.total, overlay);
        ImGui::SameLine();
        const bool comparing = p.compare_title == t.title_id;
        if (comparing) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(comparing ? "Comparing" : "Compare achievements")) {
            if (!comparing) app.LoadCompare(t.title_id);
        }
        if (comparing) ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopID();
    }

    if (p.compare_title != 0) {
        xlive::theme::Section("Achievements");
        DrawCompare(app);
    }
}

}  // namespace launcher
