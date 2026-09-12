#include "theme.h"

#include <cstdio>
#include <cstring>

#include "selawik.h"

namespace xlive::theme {

namespace {

ImU32 Col(const ImVec4& v, float alpha = 1.0f) {
    return ImGui::GetColorU32(ImVec4(v.x, v.y, v.z, v.w * alpha));
}

Fonts g_fonts;

}  // namespace

void Apply(ImGuiStyle& style, float scale, float bg_alpha) {
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.ScrollbarSize = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0.0f, 4.0f);
    if (scale != 1.0f) style.ScaleAllSizes(scale);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = ImVec4(kBg.x, kBg.y, kBg.z, bg_alpha);
    c[ImGuiCol_ChildBg] = ImVec4(kPanel.x, kPanel.y, kPanel.z, bg_alpha < 1.0f ? 0.85f : 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(kPanel.x, kPanel.y, kPanel.z, 0.98f);
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = kPanelHi;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.205f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.25f, 0.27f, 1.0f);
    c[ImGuiCol_TitleBg] = kRail;
    c[ImGuiCol_TitleBgActive] = kRail;
    c[ImGuiCol_MenuBarBg] = kRail;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kPanelHi;
    c[ImGuiCol_ScrollbarGrabHovered] = kBorder;
    c[ImGuiCol_ScrollbarGrabActive] = kGreen;
    c[ImGuiCol_CheckMark] = kLime;
    c[ImGuiCol_SliderGrab] = kGreen;
    c[ImGuiCol_SliderGrabActive] = kGreenHi;
    c[ImGuiCol_Button] = kGreen;
    c[ImGuiCol_ButtonHovered] = kGreenHi;
    c[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.76f, 0.28f, 1.0f);
    c[ImGuiCol_Header] = kPanelHi;
    c[ImGuiCol_HeaderHovered] = ImVec4(0.19f, 0.205f, 0.22f, 1.0f);
    c[ImGuiCol_HeaderActive] = kGreen;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_SeparatorHovered] = kBorder;
    c[ImGuiCol_SeparatorActive] = kGreen;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = kPanel;
    c[ImGuiCol_TabHovered] = kGreenHi;
    c[ImGuiCol_TabSelected] = kGreen;
    c[ImGuiCol_TabSelectedOverline] = kLime;
    c[ImGuiCol_TabDimmed] = kPanel;
    c[ImGuiCol_TabDimmedSelected] = kGreen;
    c[ImGuiCol_PlotHistogram] = kGreen;
    c[ImGuiCol_PlotHistogramHovered] = kGreenHi;
    c[ImGuiCol_TextSelectedBg] = ImVec4(kGreen.x, kGreen.y, kGreen.z, 0.45f);
    c[ImGuiCol_NavCursor] = ImVec4(kLime.x, kLime.y, kLime.z, 0.85f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

Fonts LoadFonts(ImGuiIO& io, float body_size, const char* override_path) {
    Fonts fonts;
    if (override_path && *override_path) {
        fonts.body = io.Fonts->AddFontFromFileTTF(override_path, body_size);
        if (fonts.body) {
            fonts.heading = io.Fonts->AddFontFromFileTTF(override_path, body_size * 1.3f);
            fonts.title = io.Fonts->AddFontFromFileTTF(override_path, body_size * 1.7f);
        } else {
            std::fprintf(stderr, "[xlive] could not load font %s; using Selawik\n", override_path);
        }
    }
    if (!fonts.body) {
        // The bytes live in the binary; the atlas must not free them.
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        std::snprintf(config.Name, sizeof(config.Name), "Selawik %.0fpx", body_size);
        fonts.body = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(selawik::kRegular), int(selawik::kRegularSize), body_size, &config);
        std::snprintf(config.Name, sizeof(config.Name), "Selawik Semibold %.0fpx", body_size * 1.3f);
        fonts.heading = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(selawik::kSemibold), int(selawik::kSemiboldSize),
            body_size * 1.3f, &config);
        std::snprintf(config.Name, sizeof(config.Name), "Selawik Semibold %.0fpx", body_size * 1.7f);
        fonts.title = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(selawik::kSemibold), int(selawik::kSemiboldSize),
            body_size * 1.7f, &config);
    }
    if (!fonts.heading) fonts.heading = fonts.body;
    if (!fonts.title) fonts.title = fonts.heading;
    io.FontDefault = fonts.body;
    g_fonts = fonts;
    return fonts;
}

void Section(const char* label) {
    ImGui::Spacing();
    ImGui::PushFont(g_fonts.heading);
    ImGui::TextColored(kLime, "%s", label);
    ImGui::PopFont();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float y = max.y + 3.0f;
    const float right = min.x + ImGui::GetContentRegionAvail().x;
    // A short lime stroke under the word, the rest of the rule in the
    // border colour.
    draw->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), Col(kLime), 2.0f);
    draw->AddLine(ImVec2(max.x, y), ImVec2(right, y), Col(kBorder), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

bool RailItem(const char* label, bool selected, const char* badge) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f + 6.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##rail", ImVec2(width, height));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    if (selected) {
        draw->AddRectFilled(pos, max, Col(kGreen), style.FrameRounding);
        // The gloss the 360's blades had: lighter on top.
        draw->AddRectFilledMultiColor(pos, ImVec2(max.x, pos.y + height * 0.5f),
                                      IM_COL32(255, 255, 255, 34), IM_COL32(255, 255, 255, 34),
                                      IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        draw->AddRectFilled(pos, ImVec2(pos.x + 4.0f, max.y), Col(kLime), style.FrameRounding,
                            ImDrawFlags_RoundCornersLeft);
    } else if (hovered) {
        draw->AddRectFilled(pos, max, Col(kPanelHi), style.FrameRounding);
    }
    if (ImGui::IsItemFocused() && !selected) {
        draw->AddRect(pos, max, Col(kLime, 0.6f), style.FrameRounding);
    }
    const ImVec2 text_pos(pos.x + 14.0f, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
    draw->AddText(text_pos, selected ? IM_COL32_WHITE : Col(hovered ? kText : kMuted), label);
    if (badge && *badge) {
        ImGui::PushFont(g_fonts.body);
        const ImVec2 size = ImGui::CalcTextSize(badge);
        ImGui::PopFont();
        const float pad = 7.0f;
        const ImVec2 bmax(max.x - 10.0f, pos.y + (height + size.y) * 0.5f + 2.0f);
        const ImVec2 bmin(bmax.x - size.x - pad * 2.0f, pos.y + (height - size.y) * 0.5f - 2.0f);
        draw->AddRectFilled(bmin, bmax, selected ? Col(kLime) : Col(kGreen), 99.0f);
        draw->AddText(ImVec2(bmin.x + pad, bmin.y + 2.0f), selected ? IM_COL32(20, 30, 15, 255) : IM_COL32_WHITE, badge);
    }
    return clicked;
}

void Gamercard(const char* gamertag, unsigned gamerscore, bool online, const char* status,
               float width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    if (width <= 0.0f) width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad = 10.0f;
    const float name_h = g_fonts.heading->FontSize;
    const float line_h = ImGui::GetTextLineHeight();
    const float height = pad * 2.0f + name_h + line_h * (status && *status ? 2.0f : 1.0f) + 6.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    // A green tile with a gloss, the way the dashboard's card was.
    draw->AddRectFilled(pos, max, Col(kGreen), style.ChildRounding);
    draw->AddRectFilledMultiColor(pos, ImVec2(max.x, pos.y + height * 0.45f),
                                  IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 40),
                                  IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    draw->AddRect(pos, max, Col(kGreenHi), style.ChildRounding);
    float y = pos.y + pad;
    draw->AddText(g_fonts.heading, name_h, ImVec2(pos.x + pad, y), IM_COL32_WHITE, gamertag);
    y += name_h + 4.0f;
    // The gamerscore, with the "G" in a circle the way the console wrote it.
    const float r = line_h * 0.42f;
    draw->AddCircleFilled(ImVec2(pos.x + pad + r, y + line_h * 0.5f), r, IM_COL32_WHITE);
    const ImVec2 g = ImGui::CalcTextSize("G");
    draw->AddText(ImVec2(pos.x + pad + r - g.x * 0.5f, y + (line_h - g.y) * 0.5f),
                  Col(kGreen), "G");
    char score[32];
    std::snprintf(score, sizeof(score), "%u", gamerscore);
    draw->AddText(ImVec2(pos.x + pad + r * 2.0f + 6.0f, y), IM_COL32_WHITE, score);
    if (status && *status) {
        y += line_h + 2.0f;
        const ImU32 dot = online ? Col(kLime) : IM_COL32(200, 200, 200, 160);
        draw->AddCircleFilled(ImVec2(pos.x + pad + r, y + line_h * 0.5f), r * 0.5f, dot);
        draw->AddText(ImVec2(pos.x + pad + r * 2.0f + 6.0f, y), IM_COL32(235, 245, 230, 230), status);
    }
    ImGui::Dummy(ImVec2(width, height));
}

void GlossLastItem() {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        min, ImVec2(max.x, min.y + (max.y - min.y) * 0.5f), IM_COL32(255, 255, 255, 28),
        IM_COL32(255, 255, 255, 28), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
}

}  // namespace xlive::theme
