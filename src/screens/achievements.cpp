// A title's achievements: name, the locked or unlocked description, score,
// unlocked date. Hidden ones are "Secret achievement" until unlocked, which
// is what the console did. The tiles are the SPA's own, via the server.
#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void DrawAchievements(App& app) {
    if (app.config.titles.empty()) {
        ImGui::TextDisabled("Add a title on the Home tab first.");
        return;
    }

    int index = app.achievements.title_index < 0 ? 0 : app.achievements.title_index;
    if (index >= int(app.config.titles.size())) index = 0;
    ImGui::BeginDisabled(app.achievements.ticket != 0);
    const bool refresh = xlive::theme::PageHeader("Achievements", nullptr, "Refresh");
    ImGui::EndDisabled();
    if (refresh) app.LoadAchievements(index);

    // The picker.
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##title", app.config.titles[size_t(index)].name.c_str())) {
        for (int i = 0; i < int(app.config.titles.size()); ++i) {
            const bool selected = i == index;
            if (ImGui::Selectable(app.config.titles[size_t(i)].name.c_str(), selected)) {
                if (i != app.achievements.title_index || !app.achievements.loaded) {
                    app.LoadAchievements(i);
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (app.achievements.title_index < 0 && app.achievements.ticket == 0) {
        app.LoadAchievements(index);
    }

    if (app.achievements.ticket != 0) {
        ImGui::TextDisabled("loading...");
        return;
    }
    if (!app.achievements.error.empty()) {
        ImGui::TextColored(xlive::theme::kRed, "%s", app.achievements.error.c_str());
        if (app.achievements.error == "no_title") {
            ImGui::TextWrapped("This server has not imported the title. "
                               "tools/spa_import.py in XenonLive does that.");
        }
        return;
    }
    if (!app.achievements.loaded) return;

    const xlive::Client::TitleInfo& title = app.achievements.result.title;
    unsigned unlocked = 0;
    for (const auto& a : title.achievements) unlocked += a.unlocked;
    if (const Image tile_image = app.images.Title(title.title_id); tile_image.texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(tile_image.texture), ImVec2(48.0f, 48.0f));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(app.fonts.heading);
    ImGui::Text("%s", title.name.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%u / %u G, %u of %zu unlocked", title.gamerscore, title.max_gamerscore,
                        unlocked, title.achievements.size());
    // How far along, as a bar: the dashboard showed one per game.
    {
        char overlay[8] = {};
        const float frac = title.achievements.empty()
                               ? 0.0f
                               : float(unlocked) / float(title.achievements.size());
        ImGui::ProgressBar(frac, ImVec2(260.0f, 6.0f), overlay);
    }
    ImGui::EndGroup();
    ImGui::Spacing();

    const float tile = 64.0f;
    for (const xlive::Client::Achievement& a : title.achievements) {
        ImGui::PushID(a.id);
        ImGui::BeginChild("ach", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        const bool secret = a.hidden && !a.unlocked;

        // The tile, from the SPA's own art via the server. Locked ones are
        // drawn dimmed; a secret one shows no art at all, which is what the
        // console did. While the image has not arrived, an empty square
        // holds the space so the text does not jump when it does.
        const Image image = secret ? Image{} : app.images.Achievement(title.title_id, a.id);
        if (image.texture) {
            const ImVec4 tint = a.unlocked ? ImVec4(1, 1, 1, 1) : ImVec4(0.55f, 0.55f, 0.55f, 0.8f);
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(image.texture), ImVec2(tile, tile),
                               ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
        } else {
            ImGui::Dummy(ImVec2(tile, tile));
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                ImGui::GetColorU32(ImGuiCol_Border));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (a.unlocked) {
            ImGui::TextColored(xlive::theme::kLime, "%s", a.name.c_str());
        } else {
            ImGui::TextDisabled("%s", secret ? "Secret achievement" : a.name.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u G", a.score);
        if (a.unlocked) {
            ImGui::TextWrapped("%s", a.unlocked_description.empty() ? a.locked_description.c_str()
                                                                     : a.unlocked_description.c_str());
            if (!a.unlocked_at.empty()) {
                ImGui::TextDisabled("unlocked %s", a.unlocked_at.substr(0, 10).c_str());
            }
        } else if (!secret) {
            ImGui::TextWrapped("%s", a.locked_description.c_str());
        } else {
            ImGui::TextDisabled("Continue playing to unlock this secret achievement.");
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopID();
    }
}

}  // namespace launcher
