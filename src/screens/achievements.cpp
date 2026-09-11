// A title's achievements: name, the locked or unlocked description, score,
// unlocked date. Hidden ones are "Secret achievement" until unlocked, which
// is what the console did. No images in v1: the server does not serve them.
#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void DrawAchievements(App& app) {
    if (app.config.titles.empty()) {
        ImGui::TextDisabled("Add a title on the Home tab first.");
        return;
    }

    // The picker.
    int index = app.achievements.title_index < 0 ? 0 : app.achievements.title_index;
    if (index >= int(app.config.titles.size())) index = 0;
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
    ImGui::SameLine();
    ImGui::BeginDisabled(app.achievements.ticket != 0);
    if (ImGui::SmallButton("Refresh")) app.LoadAchievements(index);
    ImGui::EndDisabled();
    if (app.achievements.title_index < 0 && app.achievements.ticket == 0) {
        app.LoadAchievements(index);
    }

    if (app.achievements.ticket != 0) {
        ImGui::TextDisabled("loading...");
        return;
    }
    if (!app.achievements.error.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", app.achievements.error.c_str());
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
    ImGui::Text("%s", title.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u G, %u of %zu unlocked", title.gamerscore, title.max_gamerscore,
                        unlocked, title.achievements.size());
    ImGui::Separator();

    for (const xlive::Client::Achievement& a : title.achievements) {
        ImGui::PushID(a.id);
        ImGui::BeginChild("ach", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        const bool secret = a.hidden && !a.unlocked;
        if (a.unlocked) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", a.name.c_str());
        } else {
            ImGui::TextDisabled("%s", secret ? "Secret achievement" : a.name.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u G", a.score);
        if (a.unlocked) {
            ImGui::TextWrapped("%s", a.unlocked_description.empty() ? a.locked_description.c_str()
                                                                     : a.unlocked_description.c_str());
            if (!a.unlocked_at.empty()) ImGui::TextDisabled("unlocked %s", a.unlocked_at.c_str());
        } else if (!secret) {
            ImGui::TextWrapped("%s", a.locked_description.c_str());
        } else {
            ImGui::TextDisabled("Continue playing to unlock this secret achievement.");
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

}  // namespace launcher
