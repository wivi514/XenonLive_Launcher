#include "theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "i18n.h"
#include "notocjk.h"
#include "selawik.h"

namespace xlive::theme {

namespace {

constexpr float kPi = 3.14159265f;

ImU32 Col(const ImVec4& v, float alpha = 1.0f) {
    return ImGui::GetColorU32(ImVec4(v.x, v.y, v.z, v.w * alpha));
}

Fonts g_fonts;

}  // namespace

void Apply(ImGuiStyle& style, float scale, float bg_alpha) {
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(11.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.ScrollbarSize = 9.0f;
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
    // Inputs sit a shade below the panel they are on, like a slot.
    c[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.082f, 0.09f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.11f, 0.12f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.11f, 0.125f, 0.135f, 1.0f);
    c[ImGuiCol_TitleBg] = kRail;
    c[ImGuiCol_TitleBgActive] = kRail;
    c[ImGuiCol_MenuBarBg] = kRail;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kPanelHi;
    c[ImGuiCol_ScrollbarGrabHovered] = kBorder;
    c[ImGuiCol_ScrollbarGrabActive] = kGreen;
    c[ImGuiCol_CheckMark] = kLime;
    c[ImGuiCol_TextLink] = kLime;
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

Fonts LoadFonts(ImGuiIO& io, float body_size, const char* override_path, const char* cjk_path,
                const char* cjk_lang) {
    Fonts fonts;
    // Japanese and Korean: the launcher's own strings come from the Noto
    // Sans CJK subset compiled in (every glyph the table uses, the kana,
    // the compatibility jamo, CJK punctuation), merged into each face
    // behind Selawik. A system CJK font, when there is one, is merged
    // behind that into the body face only, with the broad ranges, so a
    // friend's message reaches glyphs the subset has not — the body face
    // is the one chat is set in, and the broad Korean range at three sizes
    // would not fit one atlas.
    static ImVector<ImWchar> subset_ranges;
    const bool cjk = cjk_lang && *cjk_lang;
    const bool ko = cjk && std::string(cjk_lang) == "ko";
    if (cjk) {
        ImFontGlyphRangesBuilder builder;
        for (const std::string& text :
             xlive::i18n::AllTexts(ko ? xlive::i18n::Lang::Ko : xlive::i18n::Lang::Ja)) {
            builder.AddText(text.c_str());
        }
        static const ImWchar extra[] = {0x3000, 0x303F, 0x3041, 0x3096, 0x30A1, 0x30FA, 0x30FC, 0x30FC,
                                        0x3131, 0x3163, 0xFF01, 0xFF60, 0};
        builder.AddRanges(extra);
        subset_ranges.clear();
        builder.BuildRanges(&subset_ranges);
    }
    const auto merge_cjk = [&](float size, bool body_face) {
        if (!cjk) return;
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.PixelSnapH = true;
        merge.FontDataOwnedByAtlas = false;
        std::snprintf(merge.Name, sizeof(merge.Name), "Noto CJK subset %.0fpx", size);
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(notocjk::kSubset),
                                       int(notocjk::kSubsetSize), size, &merge, subset_ranges.Data);
        if (body_face && cjk_path && *cjk_path) {
            ImFontConfig system;
            system.MergeMode = true;
            system.PixelSnapH = true;
            std::snprintf(system.Name, sizeof(system.Name), "CJK system %.0fpx", size);
            const ImWchar* broad = ko ? io.Fonts->GetGlyphRangesKorean() : io.Fonts->GetGlyphRangesJapanese();
            if (!io.Fonts->AddFontFromFileTTF(cjk_path, size, &system, broad)) {
                std::fprintf(stderr, "[xlive] could not load CJK font %s\n", cjk_path);
            }
        }
    };
    if (override_path && *override_path) {
        fonts.body = io.Fonts->AddFontFromFileTTF(override_path, body_size);
        if (fonts.body) {
            merge_cjk(body_size, true);
            fonts.heading = io.Fonts->AddFontFromFileTTF(override_path, body_size * 1.3f);
            merge_cjk(body_size * 1.3f, false);
            fonts.title = io.Fonts->AddFontFromFileTTF(override_path, body_size * 1.7f);
            merge_cjk(body_size * 1.7f, false);
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
        merge_cjk(body_size, true);
        std::snprintf(config.Name, sizeof(config.Name), "Selawik Semibold %.0fpx", body_size * 1.3f);
        fonts.heading = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(selawik::kSemibold), int(selawik::kSemiboldSize),
            body_size * 1.3f, &config);
        merge_cjk(body_size * 1.3f, false);
        std::snprintf(config.Name, sizeof(config.Name), "Selawik Semibold %.0fpx", body_size * 1.7f);
        fonts.title = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(selawik::kSemibold), int(selawik::kSemiboldSize),
            body_size * 1.7f, &config);
        merge_cjk(body_size * 1.7f, false);
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

bool RailItem(const char* label, bool selected, const char* badge, Icon icon) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f + 8.0f;
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
    const ImU32 ink = selected ? IM_COL32_WHITE : Col(hovered ? kText : kMuted);
    float text_x = pos.x + 14.0f;
    if (icon != Icon::None) {
        const float box = ImGui::GetTextLineHeight() * 0.95f;
        DrawIcon(draw, icon, ImVec2(pos.x + 14.0f + box * 0.5f, pos.y + height * 0.5f), box,
                 selected ? IM_COL32_WHITE : Col(hovered ? kText : kLime, hovered ? 1.0f : 0.8f));
        text_x += box + 10.0f;
    }
    const ImVec2 text_pos(text_x, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
    draw->AddText(text_pos, ink, label);
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

bool Gamercard(const char* gamertag, unsigned gamerscore, bool online, const char* status,
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
    ImGui::PushID(gamertag);
    const bool clicked = ImGui::InvisibleButton("##gamercard", ImVec2(width, height));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    // A green tile with a gloss, the way the dashboard's card was.
    draw->AddRectFilled(pos, max, Col(hovered ? kGreenHi : kGreen), style.ChildRounding);
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
    return clicked;
}


// -- icons ---------------------------------------------------------------------

void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 c, float size, ImU32 col) {
    const float s = size * 0.5f;  // half-box
    const float t = std::max(1.4f, size * 0.11f);  // stroke
    switch (icon) {
    case Icon::None:
        break;
    case Icon::Home: {
        // A roof over a body, a door in it.
        const ImVec2 roof[] = {ImVec2(c.x - s, c.y), ImVec2(c.x, c.y - s), ImVec2(c.x + s, c.y)};
        draw->AddPolyline(roof, 3, col, ImDrawFlags_None, t);
        draw->AddRect(ImVec2(c.x - s * 0.7f, c.y - s * 0.05f), ImVec2(c.x + s * 0.7f, c.y + s),
                      col, 0.0f, 0, t);
        draw->AddRectFilled(ImVec2(c.x - s * 0.18f, c.y + s * 0.35f), ImVec2(c.x + s * 0.18f, c.y + s), col);
        break;
    }
    case Icon::Friends: {
        // Two people, one a little behind the other.
        const float r = s * 0.32f;
        draw->AddCircle(ImVec2(c.x + s * 0.3f, c.y - s * 0.35f), r, col, 0, t);
        draw->AddCircleFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), r, col);
        draw->PathArcTo(ImVec2(c.x - s * 0.3f, c.y + s * 0.85f), s * 0.62f, kPi, 2.0f * kPi, 12);
        draw->PathFillConvex(col);
        draw->PathArcTo(ImVec2(c.x + s * 0.3f, c.y + s * 0.85f), s * 0.62f, kPi * 1.15f, 2.0f * kPi, 10);
        draw->PathStroke(col, ImDrawFlags_None, t);
        break;
    }
    case Icon::Messages: {
        // A speech bubble.
        draw->AddRect(ImVec2(c.x - s, c.y - s * 0.8f), ImVec2(c.x + s, c.y + s * 0.4f), col, s * 0.35f, 0, t);
        const ImVec2 tail[] = {ImVec2(c.x - s * 0.5f, c.y + s * 0.4f), ImVec2(c.x - s * 0.6f, c.y + s),
                               ImVec2(c.x - s * 0.05f, c.y + s * 0.4f)};
        draw->AddConvexPolyFilled(tail, 3, col);
        break;
    }
    case Icon::Invites: {
        // An envelope.
        draw->AddRect(ImVec2(c.x - s, c.y - s * 0.7f), ImVec2(c.x + s, c.y + s * 0.7f), col, s * 0.15f, 0, t);
        const ImVec2 flap[] = {ImVec2(c.x - s, c.y - s * 0.6f), ImVec2(c.x, c.y + s * 0.15f),
                               ImVec2(c.x + s, c.y - s * 0.6f)};
        draw->AddPolyline(flap, 3, col, ImDrawFlags_None, t);
        break;
    }
    case Icon::Achievements: {
        // A trophy: cup, handles, stem, base.
        draw->AddRectFilled(ImVec2(c.x - s * 0.55f, c.y - s), ImVec2(c.x + s * 0.55f, c.y + s * 0.15f), col, s * 0.3f,
                            ImDrawFlags_RoundCornersBottom);
        draw->PathArcTo(ImVec2(c.x - s * 0.7f, c.y - s * 0.55f), s * 0.35f, kPi * 0.5f, kPi * 1.5f, 8);
        draw->PathStroke(col, ImDrawFlags_None, t);
        draw->PathArcTo(ImVec2(c.x + s * 0.7f, c.y - s * 0.55f), s * 0.35f, -kPi * 0.5f, kPi * 0.5f, 8);
        draw->PathStroke(col, ImDrawFlags_None, t);
        draw->AddRectFilled(ImVec2(c.x - s * 0.12f, c.y + s * 0.15f), ImVec2(c.x + s * 0.12f, c.y + s * 0.6f), col);
        draw->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y + s * 0.6f), ImVec2(c.x + s * 0.5f, c.y + s), col, s * 0.1f);
        break;
    }
    case Icon::Issues: {
        // A bug: body, head, three legs a side.
        draw->AddEllipseFilled(ImVec2(c.x, c.y + s * 0.15f), ImVec2(s * 0.5f, s * 0.7f), col);
        draw->AddCircleFilled(ImVec2(c.x, c.y - s * 0.65f), s * 0.3f, col);
        for (int i = 0; i < 3; ++i) {
            const float y = c.y - s * 0.25f + i * s * 0.4f;
            draw->AddLine(ImVec2(c.x - s * 0.45f, y), ImVec2(c.x - s, y + s * 0.15f), col, t);
            draw->AddLine(ImVec2(c.x + s * 0.45f, y), ImVec2(c.x + s, y + s * 0.15f), col, t);
        }
        break;
    }
    case Icon::Support: {
        // A heart.
        const float r = s * 0.48f;
        draw->AddCircleFilled(ImVec2(c.x - r, c.y - s * 0.3f), r, col);
        draw->AddCircleFilled(ImVec2(c.x + r, c.y - s * 0.3f), r, col);
        const ImVec2 tip[] = {ImVec2(c.x - s * 0.95f, c.y - s * 0.1f), ImVec2(c.x + s * 0.95f, c.y - s * 0.1f),
                              ImVec2(c.x, c.y + s)};
        draw->AddConvexPolyFilled(tip, 3, col);
        break;
    }
    case Icon::Account: {
        draw->AddCircle(ImVec2(c.x, c.y - s * 0.4f), s * 0.42f, col, 0, t);
        draw->PathArcTo(ImVec2(c.x, c.y + s * 1.05f), s * 0.85f, kPi * 1.12f, kPi * 1.88f, 12);
        draw->PathStroke(col, ImDrawFlags_None, t);
        break;
    }
    }
}

// -- page furniture ----------------------------------------------------------------

bool PageHeader(const char* title, const char* subtitle, const char* button) {
    ImGui::PushFont(g_fonts.title);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    const float title_h = ImGui::GetItemRectSize().y;
    if (subtitle && *subtitle) {
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "%s", subtitle);
    }
    bool clicked = false;
    if (button && *button) {
        const float w = ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        const float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + (title_h - ImGui::GetFrameHeight()) * 0.5f);
        clicked = SmallSecondaryButton(button);
    }
    const float y = ImGui::GetCursorScreenPos().y + 2.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float left = ImGui::GetCursorScreenPos().x;
    const float right = left + ImGui::GetContentRegionAvail().x;
    draw->AddLine(ImVec2(left, y), ImVec2(left + 46.0f, y), Col(kLime), 2.0f);
    draw->AddLine(ImVec2(left + 46.0f, y), ImVec2(right, y), Col(kBorder), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    return clicked;
}

bool SecondaryButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.24f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGreen);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Col(kBorder),
                  ImGui::GetStyle().FrameRounding);
    return clicked;
}

bool SmallSecondaryButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, kPanelHi);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.24f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGreen);
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(3);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Col(kBorder),
                  ImGui::GetStyle().FrameRounding);
    return clicked;
}

void RailBackground(ImVec2 min, ImVec2 max) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 top = Col(kRail);
    const ImU32 bottom = Col(ImVec4(kRail.x * 0.7f, kRail.y * 0.7f, kRail.z * 0.7f, 1.0f));
    draw->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
    draw->AddLine(ImVec2(max.x - 0.5f, min.y), ImVec2(max.x - 0.5f, max.y), Col(kBorder, 0.8f));
}

void ContentBackground(ImVec2 min, ImVec2 max) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 top = Col(ImVec4(kBg.x * 1.55f, kBg.y * 1.55f, kBg.z * 1.55f, 1.0f));
    const ImU32 bottom = Col(kBg);
    draw->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
    // The glow: a stack of ever-smaller, ever-fainter discs. Cheap, and
    // reads as a soft light behind the top-left of the page.
    const ImVec2 at(min.x + 40.0f, min.y - 60.0f);
    const float reach = std::min(max.x - min.x, max.y - min.y) * 0.9f;
    for (int i = 0; i < 7; ++i) {
        const float r = reach * (1.0f - i * 0.13f);
        draw->AddCircleFilled(at, r, IM_COL32(60, 139, 42, 5), 48);
    }
}

void GlossLastItem() {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        min, ImVec2(max.x, min.y + (max.y - min.y) * 0.5f), IM_COL32(255, 255, 255, 28),
        IM_COL32(255, 255, 255, 28), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
}

}  // namespace xlive::theme
