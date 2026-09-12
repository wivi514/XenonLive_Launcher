// Issues: what a port captured, waiting to be sent or deleted, and every
// report anyone has sent, searchable by its words.
//
// Left column: the captures on this machine, then the reports the search
// found. Right: the selected capture — its screenshot, the game, when, the
// machine, the files — with the three boxes a report needs and Send /
// Delete; or the selected report, which is only ever its words and state.
// Nothing leaves this machine until Send.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

using Issue = xlive::Client::Issue;
using xlive::theme::kAmber;
using xlive::theme::kGreen;
using xlive::theme::kLime;
using xlive::theme::kMuted;
using xlive::theme::kPanel;
using xlive::theme::kPanelHi;
using xlive::theme::kRed;

// "2026-09-12T13:45:10Z" -> "2026-09-12 13:45".
std::string When(const std::string& rfc3339) {
    if (rfc3339.size() < 16) return rfc3339;
    return rfc3339.substr(0, 10) + " " + rfc3339.substr(11, 5);
}

std::string Size(std::uintmax_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MiB", double(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024) std::snprintf(buf, sizeof(buf), "%.0f KiB", double(bytes) / 1024.0);
    else std::snprintf(buf, sizeof(buf), "%ju B", bytes);
    return buf;
}

// The game's name for a title id: the launcher's own list first, then what
// the capture said, then the id.
std::string GameName(const App& app, uint32_t title_id, const std::string& fallback) {
    for (const auto& t : app.config.titles) {
        if (t.title_id == title_id && !t.name.empty()) return t.name;
    }
    if (!fallback.empty()) return fallback;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", title_id);
    return buf;
}

ImVec4 StateColour(const std::string& state) {
    if (state == "fixed") return kLime;
    if (state == "closed") return kMuted;
    return kAmber;
}

// One row of the left column: a heading line and a muted line under it.
bool Row(App& app, const char* id, const char* heading, const std::string& sub, bool selected,
         const ImVec4* mark = nullptr) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = app.fonts.heading->FontSize + ImGui::GetTextLineHeight() + 14.0f;
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, height));
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
    if (ImGui::IsItemFocused() && !selected) {
        draw->AddRect(pos, max, ImGui::GetColorU32(kLime), 5.0f, 0, 2.0f);
    }
    ImGui::PushClipRect(pos, ImVec2(max.x - 6.0f, max.y), true);
    ImGui::PushFont(app.fonts.heading);
    draw->AddText(ImVec2(pos.x + 12.0f, pos.y + 6.0f), ImGui::GetColorU32(ImGuiCol_Text), heading);
    ImGui::PopFont();
    if (mark) {
        draw->AddCircleFilled(ImVec2(max.x - 14.0f, pos.y + 6.0f + app.fonts.heading->FontSize * 0.5f),
                              4.0f, ImGui::GetColorU32(*mark));
    }
    draw->AddText(ImVec2(pos.x + 12.0f, pos.y + 8.0f + app.fonts.heading->FontSize),
                  ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled), sub.c_str());
    ImGui::PopClipRect();
    ImGui::PopID();
    return clicked;
}

void DrawCaptureDetail(App& app, const Capture& c) {
    auto& st = app.issues;
    const bool busy = st.send_ticket != 0;

    ImGui::PushFont(app.fonts.heading);
    ImGui::TextColored(kLime, "%s",
                       c.title_id || !c.game.empty() ? GameName(app, c.title_id, c.game).c_str()
                                                     : c.id.c_str());
    ImGui::PopFont();
    if (!c.problem.empty()) {
        // Not a capture the launcher can read: no form, just the way out.
        ImGui::TextColored(kRed, "This capture is broken");
        ImGui::TextWrapped("%s", c.problem.c_str());
        ImGui::TextWrapped("It cannot be sent. Delete it, or capture again in the game.");
        ImGui::TextDisabled("%s", c.dir.string().c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRed);
        if (ImGui::Button("Delete it", ImVec2(220.0f, 0.0f))) app.DeleteCapture();
        ImGui::PopStyleColor(2);
        return;
    }
    ImGui::TextDisabled("%s%scaptured %s%s%s", c.game_version.c_str(),
                        c.game_version.empty() ? "" : "   ", When(c.captured_at).c_str(),
                        c.trigger.empty() ? "" : " with ", c.trigger.c_str());
    ImGui::Spacing();

    // The screenshot, if there is one, at most 340 wide.
    for (const CaptureFile& f : c.files) {
        if (!f.present || f.type.compare(0, 6, "image/") != 0) continue;
        const Image img = app.images.Local(f.path);
        if (!img.texture) continue;
        const float w = std::min(340.0f, ImGui::GetContentRegionAvail().x);
        const float h = w * float(img.height) / float(std::max(1, img.width));
            ImGui::Image(reinterpret_cast<ImTextureID>(img.texture), ImVec2(w, h));
        ImGui::Spacing();
        break;
    }

    // What goes with it. The player sees every file and every fact
    // before deciding.
    ImGui::TextUnformatted("What will be sent with your words");
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    for (const CaptureFile& f : c.files) {
        ImGui::Bullet();
        ImGui::SameLine();
        if (f.present) {
            ImGui::TextWrapped("%s (%s)%s%s", f.name.c_str(), Size(f.size).c_str(),
                               f.what.empty() ? "" : " - ", f.what.c_str());
        } else {
            ImGui::PopStyleColor();
            ImGui::TextWrapped("%s - missing", f.name.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        }
    }
    if (!c.system.empty()) {
        std::string line;
        for (const auto& [key, value] : c.system) {
            if (!line.empty()) line += ", ";
            line += key + " " + value;
        }
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("your machine: %s", line.c_str());
    }
    ImGui::TextWrapped("Only the developer sees the files and your machine. Everyone can read the "
                       "title, what happened and how to reproduce, so the next person with the same "
                       "bug finds yours.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::BeginDisabled(busy);
    ImGui::TextUnformatted("Title");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##title", "one line: what is wrong", st.title, sizeof(st.title));
    ImGui::TextUnformatted("What happened");
    ImGui::InputTextMultiline("##summary", st.summary, sizeof(st.summary),
                              ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f));
    ImGui::TextUnformatted("How to make it happen again");
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "if you know");
    ImGui::InputTextMultiline("##steps", st.steps, sizeof(st.steps),
                              ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));
    ImGui::Spacing();
    if (ImGui::Button("Send to the developer", ImVec2(220.0f, 0.0f))) app.SendCapture();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRed);
    if (ImGui::Button("Delete, it was nothing", ImVec2(220.0f, 0.0f))) app.DeleteCapture();
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Sending...");
    } else if (!st.error.empty()) {
        ImGui::TextColored(kRed, "%s", st.error.c_str());
    }
}

void DrawReportDetail(App& app, const Issue& is) {
    ImGui::PushFont(app.fonts.heading);
    ImGui::TextColored(kLime, "%s", is.title.c_str());
    ImGui::PopFont();
    ImGui::TextColored(StateColour(is.state), "%s", is.state.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s %s", GameName(app, is.title_id, "").c_str(), is.game_version.c_str());
    ImGui::TextDisabled("reported %s%s", When(is.created_at).c_str(), is.mine ? " by you" : "");
    ImGui::Spacing();
    xlive::theme::Section("What happened");
    ImGui::TextWrapped("%s", is.summary.c_str());
    ImGui::Spacing();
    xlive::theme::Section("How to make it happen again");
    if (is.steps.empty()) ImGui::TextDisabled("not given");
    else ImGui::TextWrapped("%s", is.steps.c_str());
    if (is.mine) {
        ImGui::Spacing();
        ImGui::BeginDisabled(app.issues.delete_ticket != 0);
        ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRed);
        if (ImGui::Button("Delete my report")) app.DeleteReport(is.id);
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
    }
}

}  // namespace

void DrawIssues(App& app) {
    auto& st = app.issues;
    if (xlive::theme::PageHeader("Issues", "bug reports", "Rescan")) app.RescanCaptures();
    // Opened by a hook rather than the blade, or opened before the client
    // was online: the first search still happens.
    if (!st.searched && st.search_ticket == 0 && app.client && app.client->online()) {
        app.SearchIssues();
    }

    // -- left: captures, then reports ---------------------------------------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::BeginChild("issues_list", ImVec2(300.0f * app.ui_scale, 0.0f), ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();

    ImGui::PushFont(app.fonts.heading);
    ImGui::TextColored(kLime, "Your captures");
    ImGui::PopFont();
    if (st.captures.empty()) {
        ImGui::TextDisabled("none");
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Press the capture key in the game when something goes wrong. It "
                           "shows up here for you to describe and send, or delete.");
        ImGui::PopStyleColor();
    }
    for (size_t i = 0; i < st.captures.size(); ++i) {
        const Capture& c = st.captures[i];
        const std::string sub = When(c.captured_at) + (c.problem.empty() ? "" : "  (broken)");
        const ImVec4 mark = c.problem.empty() ? kAmber : kRed;
        const std::string heading =
            c.title_id || !c.game.empty() ? GameName(app, c.title_id, c.game) : c.id;
        if (Row(app, c.id.c_str(), heading.c_str(), sub, st.selected_capture == int(i), &mark)) {
            app.SelectCapture(int(i));
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushFont(app.fonts.heading);
    ImGui::TextColored(kLime, "Reported");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("Search before you send: yours may be known.");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.0f);
    const bool go = ImGui::InputTextWithHint("##q", "words to look for", st.query, sizeof(st.query),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    if (go || (st.searched && st.search_ticket == 0 && st.searched_for != st.query &&
               ImGui::IsItemDeactivatedAfterEdit())) {
        app.SearchIssues();
    }
    if (st.search_ticket != 0) {
        ImGui::TextDisabled("Searching...");
    } else if (!st.search_error.empty()) {
        ImGui::TextColored(kRed, "%s", st.search_error.c_str());
    } else if (st.searched && st.results.empty()) {
        ImGui::TextDisabled(st.searched_for.empty() ? "nothing reported yet" : "nothing matches");
    }
    for (size_t i = 0; i < st.results.size(); ++i) {
        const Issue& is = st.results[i];
        const std::string sub = is.state + "   " + When(is.created_at).substr(0, 10) +
                                (is.mine ? "   yours" : "");
        char id[32];
        std::snprintf(id, sizeof(id), "r%lld", static_cast<long long>(is.id));
        const ImVec4 mark = StateColour(is.state);
        if (Row(app, id, is.title.c_str(), sub, st.selected_report == int(i), &mark)) {
            st.selected_report = int(i);
            st.selected_capture = -1;
        }
    }
    ImGui::EndChild();

    // -- right: the selection -------------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("issues_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    if (st.selected_capture >= 0 && st.selected_capture < int(st.captures.size())) {
        DrawCaptureDetail(app, st.captures[size_t(st.selected_capture)]);
    } else if (st.selected_report >= 0 && st.selected_report < int(st.results.size())) {
        DrawReportDetail(app, st.results[size_t(st.selected_report)]);
    } else {
        ImGui::PushFont(app.fonts.heading);
        ImGui::TextColored(kLime, "Bug reports");
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("When something goes wrong in a game, press its capture key: it saves a "
                           "screenshot, the log from the last minute and what your machine is, and "
                           "the capture appears on the left. Pick it, say what happened and how to "
                           "make it happen again, and send it - or delete it if it was nothing.");
        ImGui::Spacing();
        ImGui::TextWrapped("Everyone can read the words of every report and search them. Only the "
                           "developer sees the screenshot, the log and your machine.");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

}  // namespace launcher
