// Messages: a column of people on the left (everyone with a conversation,
// newest first, then the rest of the friends list), the open conversation on
// the right as bubbles — theirs on the left, yours on the right in green —
// and a box to write in, with the count against the 256-character limit.
// The server keeps the last 20 between two people; this screen shows what
// it keeps and no more.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

using Message = xlive::Client::Message;
using xlive::theme::kGreen;
using xlive::theme::kLime;
using xlive::theme::kMuted;
using xlive::theme::kPanelHi;
using xlive::theme::kRed;

size_t Utf8Length(const char* text) {
    size_t n = 0;
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(text); *c; ++c) {
        n += (*c & 0xC0) != 0x80;
    }
    return n;
}

// "2026-09-12T14:03:22Z" -> "09-12 14:03": the day and the minute; the
// year only matters in an archive and this is not one.
std::string When(const std::string& rfc3339) {
    if (rfc3339.size() < 16) return rfc3339;
    return rfc3339.substr(5, 5) + " " + rfc3339.substr(11, 5);
}

// One entry of the people column.
bool DrawPerson(App& app, uint64_t xuid, const std::string& gamertag, const Message* latest,
                bool online, bool selected) {
    ImGui::PushID(int(xuid & 0x7FFFFFFF));
    ImGui::PushID(int(xuid >> 32));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = app.fonts.heading->FontSize + ImGui::GetTextLineHeight() + 16.0f;
    const bool clicked = ImGui::InvisibleButton("##person", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    if (selected) {
        draw->AddRectFilled(pos, max, ImGui::GetColorU32(kGreen), 5.0f);
        draw->AddRectFilled(pos, ImVec2(pos.x + 4.0f, max.y), ImGui::GetColorU32(kLime), 5.0f,
                            ImDrawFlags_RoundCornersLeft);
    } else if (hovered) {
        draw->AddRectFilled(pos, max, ImGui::GetColorU32(kPanelHi), 5.0f);
    }
    // The dot, the name, the unread badge.
    draw->AddCircleFilled(ImVec2(pos.x + 14.0f, pos.y + 8.0f + app.fonts.heading->FontSize * 0.5f),
                          4.0f, ImGui::GetColorU32(online ? kLime : kMuted));
    draw->AddText(app.fonts.heading, app.fonts.heading->FontSize, ImVec2(pos.x + 26.0f, pos.y + 8.0f),
                  selected ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text), gamertag.c_str());
    const auto unread = app.messages.unread.find(xuid);
    if (unread != app.messages.unread.end() && unread->second > 0) {
        char badge[16];
        std::snprintf(badge, sizeof(badge), "%d", unread->second);
        const ImVec2 size = ImGui::CalcTextSize(badge);
        const ImVec2 bmax(max.x - 10.0f, pos.y + 8.0f + size.y + 2.0f);
        const ImVec2 bmin(bmax.x - size.x - 14.0f, pos.y + 6.0f);
        draw->AddRectFilled(bmin, bmax, ImGui::GetColorU32(kLime), 99.0f);
        draw->AddText(ImVec2(bmin.x + 7.0f, bmin.y + 2.0f), IM_COL32(20, 30, 15, 255), badge);
    }
    // The latest line, clipped to the row.
    if (latest) {
        const std::string line = (latest->from_xuid == xuid ? "" : "You: ") + latest->body;
        const float y = pos.y + 10.0f + app.fonts.heading->FontSize;
        draw->PushClipRect(ImVec2(pos.x + 26.0f, y), ImVec2(max.x - 10.0f, max.y), true);
        draw->AddText(ImVec2(pos.x + 26.0f, y),
                      selected ? IM_COL32(235, 245, 230, 220) : ImGui::GetColorU32(kMuted),
                      line.c_str());
        draw->PopClipRect();
    }
    ImGui::PopID();
    ImGui::PopID();
    return clicked;
}

void DrawBubble(App& app, const Message& m, bool mine, float column_width) {
    const float max_width = column_width * 0.72f;
    const ImVec2 text_size = ImGui::CalcTextSize(m.body.c_str(), nullptr, false, max_width - 24.0f);
    const float width = text_size.x + 24.0f;
    const float height = text_size.y + ImGui::GetTextLineHeight() + 16.0f;
    if (mine) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + column_width - width);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    draw->AddRectFilled(pos, max, ImGui::GetColorU32(mine ? kGreen : kPanelHi), 8.0f);
    if (mine) {
        draw->AddRectFilledMultiColor(pos, ImVec2(max.x, pos.y + height * 0.4f),
                                      IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 26),
                                      IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }
    draw->AddText(nullptr, 0.0f, ImVec2(pos.x + 12.0f, pos.y + 8.0f),
                  mine ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text), m.body.c_str(), nullptr,
                  max_width - 24.0f);
    const std::string when = When(m.sent_at);
    const ImVec2 when_size = ImGui::CalcTextSize(when.c_str());
    draw->AddText(ImVec2(max.x - 12.0f - when_size.x, max.y - 6.0f - when_size.y),
                  mine ? IM_COL32(235, 245, 230, 170) : ImGui::GetColorU32(kMuted), when.c_str());
    ImGui::Dummy(ImVec2(width, height));
    (void)app;
}

}  // namespace

void DrawMessages(App& app) {
    App::MessagesState& m = app.messages;
    if (!m.inbox_loaded && m.inbox_ticket == 0) app.RefreshInbox();

    // -- the people column --------------------------------------------------
    ImGui::BeginChild("people", ImVec2(250.0f, 0.0f), ImGuiChildFlags_Borders);
    xlive::theme::Section("Messages");
    const auto friends = app.client->friends();
    const auto find_friend = [&](uint64_t xuid) -> const xlive::Client::Friend* {
        for (const auto& f : friends) if (f.xuid == xuid) return &f;
        return nullptr;
    };
    const uint64_t me = app.client->identity().xuid;
    std::vector<uint64_t> listed;
    for (const Message& latest : m.inbox) {
        const uint64_t other = latest.from_xuid == me ? latest.to_xuid : latest.from_xuid;
        const std::string& name = latest.from_xuid == me ? latest.to_gamertag : latest.from_gamertag;
        const auto* f = find_friend(other);
        listed.push_back(other);
        if (DrawPerson(app, other, name, &latest, f && f->presence.online(), m.peer == other)) {
            app.OpenConversation(other, name);
        }
    }
    bool any_more = false;
    for (const auto& f : friends) {
        if (!f.is_friend()) continue;
        if (std::find(listed.begin(), listed.end(), f.xuid) != listed.end()) continue;
        if (!any_more) {
            any_more = true;
            if (!listed.empty()) ImGui::TextDisabled("Friends");
        }
        if (DrawPerson(app, f.xuid, f.gamertag, nullptr, f.presence.online(), m.peer == f.xuid)) {
            app.OpenConversation(f.xuid, f.gamertag);
        }
    }
    if (listed.empty() && !any_more) {
        ImGui::TextWrapped("Add a friend on the Friends tab; messages go between friends.");
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // -- the conversation ------------------------------------------------------
    ImGui::BeginGroup();
    if (m.peer == 0) {
        ImGui::Dummy(ImVec2(0.0f, 40.0f));
        ImGui::TextDisabled("Pick someone to read your messages with them.");
        ImGui::TextDisabled("A message is up to 256 characters; the last 20 between you are kept.");
        ImGui::EndGroup();
        return;
    }

    const xlive::Client::Friend* peer = find_friend(m.peer);
    ImGui::PushFont(app.fonts.heading);
    ImGui::Text("%s", m.peer_gamertag.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (peer && peer->is_friend()) {
        ImGui::TextDisabled("%s", PresenceLine(peer->presence).c_str());
    } else {
        ImGui::TextDisabled("not a friend any more; you can read, not write");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Profile")) app.OpenProfile(m.peer, m.peer_gamertag);

    // The log fills what the input box leaves.
    const float input_height = ImGui::GetTextLineHeight() * 3.0f + 24.0f + ImGui::GetFrameHeight();
    const float log_height = ImGui::GetContentRegionAvail().y - input_height;
    ImGui::BeginChild("log", ImVec2(0.0f, log_height), ImGuiChildFlags_Borders);
    const float column = ImGui::GetContentRegionAvail().x;
    if (!m.error.empty()) {
        ImGui::TextColored(kRed, "%s", m.error.c_str());
    } else if (!m.conversation_loaded && m.conversation_ticket != 0) {
        ImGui::TextDisabled("loading...");
    } else if (m.conversation.empty()) {
        ImGui::TextDisabled("No messages yet. Say hello.");
    }
    for (const Message& message : m.conversation) {
        ImGui::PushID(int(message.id));
        DrawBubble(app, message, message.from_xuid == me, column);
        ImGui::PopID();
    }
    if (m.scroll_to_end) {
        ImGui::SetScrollHereY(1.0f);
        m.scroll_to_end = false;
    }
    ImGui::EndChild();

    // -- the box ------------------------------------------------------------------
    const bool can_write = peer && peer->is_friend() && m.send_ticket == 0;
    const size_t length = Utf8Length(m.draft);
    const bool over = length > xlive::Client::kMaxMessageLength;
    ImGui::BeginDisabled(!can_write);
    if (m.focus_draft) {
        ImGui::SetKeyboardFocusHere();
        m.focus_draft = false;
    }
    // Enter sends; Ctrl+Enter makes a new line.
    const bool enter = ImGui::InputTextMultiline(
        "##draft", m.draft, sizeof(m.draft),
        ImVec2(-90.0f, ImGui::GetTextLineHeight() * 3.0f + 12.0f),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::BeginDisabled(over || m.draft[0] == '\0');
    if (ImGui::Button("Send", ImVec2(-1.0f, 0.0f)) || (enter && !over)) app.SendDraft();
    ImGui::EndDisabled();
    ImGui::TextColored(over ? kRed : kMuted, "%zu / %zu", length, xlive::Client::kMaxMessageLength);
    ImGui::EndGroup();
    ImGui::EndDisabled();
    if (m.send_ticket != 0) ImGui::TextDisabled("sending...");
    ImGui::EndGroup();
}

}  // namespace launcher
