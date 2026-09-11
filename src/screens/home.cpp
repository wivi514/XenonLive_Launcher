// The account card, the titles with Play, Sign out — and the form that adds
// a title, because the launcher does not discover them.
#include <cstdio>
#include <cstring>
#include <sstream>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

// The add/edit form. Static: it is the one form on the one home screen.
struct TitleForm {
    char name[128] = {};
    char title_id[16] = {};
    char exe[512] = {};
    char cwd[512] = {};
    char env[1024] = {};
    int editing = -1;  // index being edited, or -1 for a new entry
    std::string error;

    void Load(const TitleEntry& entry, int index) {
        std::snprintf(name, sizeof(name), "%s", entry.name.c_str());
        std::snprintf(title_id, sizeof(title_id), "%s", TitleIdHex(entry.title_id).c_str());
        std::snprintf(exe, sizeof(exe), "%s", entry.exe.c_str());
        std::snprintf(cwd, sizeof(cwd), "%s", entry.cwd.c_str());
        std::string lines;
        for (const auto& [key, value] : entry.env) lines += key + "=" + value + "\n";
        std::snprintf(env, sizeof(env), "%s", lines.c_str());
        editing = index;
        error.clear();
    }
    void Clear() {
        std::memset(name, 0, sizeof(name));
        std::memset(title_id, 0, sizeof(title_id));
        std::memset(exe, 0, sizeof(exe));
        std::memset(cwd, 0, sizeof(cwd));
        std::memset(env, 0, sizeof(env));
        editing = -1;
        error.clear();
    }
    bool Read(TitleEntry& out) {
        if (!ParseTitleId(title_id, out.title_id) || out.title_id == 0) {
            error = "title id must be 1-8 hex digits, e.g. 58410b00 (cw_runtime --diag prints it)";
            return false;
        }
        out.name = name;
        if (out.name.empty()) out.name = TitleIdHex(out.title_id);
        out.exe = exe;
        out.cwd = cwd;
        out.env.clear();
        std::istringstream lines(env);
        std::string line;
        while (std::getline(lines, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) {
                error = "env lines are KEY=VALUE: " + line;
                return false;
            }
            out.env[line.substr(0, eq)] = line.substr(eq + 1);
        }
        error.clear();
        return true;
    }
};

TitleForm form;

}  // namespace

void DrawHome(App& app) {
    // -- the account card ------------------------------------------------
    const xlive::Identity id = app.client->identity();
    ImGui::BeginChild("account", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::Text("%s", id.gamertag.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  %u G", id.gamerscore);
    ImGui::TextDisabled("%s", app.client->status().c_str());
    ImGui::TextDisabled("%s", app.client->gateway_connected() ? "live updates on"
                                                               : "live updates off");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
    if (ImGui::SmallButton("Sign out")) app.client->SignOut();
    ImGui::EndChild();

    if (!app.config_error.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "%s", app.config_error.c_str());
    }

    // -- titles ------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Titles");
    if (app.config.titles.empty()) {
        ImGui::TextDisabled("No titles yet. Add one below.");
    }
    for (int i = 0; i < int(app.config.titles.size()); ++i) {
        const TitleEntry& entry = app.config.titles[size_t(i)];
        ImGui::PushID(i);
        ImGui::BeginChild("title", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::Text("%s", entry.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TitleIdHex(entry.title_id).c_str());
        ImGui::TextDisabled("%s", entry.exe.empty() ? "(no executable)" : entry.exe.c_str());

        const bool running_this = app.game.running() && app.running_title == i;
        const bool running_other = app.game.running() && app.running_title != i;
        if (running_this) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "running (pid %lld)",
                               (long long)app.game.pid());
        } else {
            ImGui::BeginDisabled(running_other || entry.exe.empty());
            if (ImGui::Button("Play", ImVec2(80.0f, 0.0f))) {
                std::string error;
                if (!app.Launch(i, error)) app.toasts.Push("Could not launch: " + error, 8.0);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) form.Load(entry, i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                app.config.titles.erase(app.config.titles.begin() + i);
                app.SaveConfigOrToast();
                if (form.editing == i) form.Clear();
                if (app.achievements.title_index >= int(app.config.titles.size())) {
                    app.achievements = App::AchievementsState{};
                }
                ImGui::EndChild();
                ImGui::PopID();
                break;
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    // -- add / edit ---------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText(form.editing >= 0 ? "Edit title" : "Add a title");
    const float label_w = 90.0f;
    const auto field = [&](const char* label, char* buf, size_t size) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushID(label);
        ImGui::InputText("##f", buf, size);
        ImGui::PopID();
    };
    field("Name", form.name, sizeof(form.name));
    field("Title id", form.title_id, sizeof(form.title_id));
    field("Executable", form.exe, sizeof(form.exe));
    field("Run in", form.cwd, sizeof(form.cwd));
    ImGui::TextUnformatted("Environment");
    ImGui::SameLine(label_w);
    ImGui::InputTextMultiline("##env", form.env, sizeof(form.env),
                              ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f));
    ImGui::SetCursorPosX(label_w);
    ImGui::TextDisabled("KEY=VALUE per line. Case West needs CW_VKDRAW=1 for a picture, "
                        "CW_XLIVE_ONLINE=1 and CW_XLIVE_COOP=1 for the social features.");

    if (ImGui::Button(form.editing >= 0 ? "Save" : "Add")) {
        TitleEntry entry;
        if (form.Read(entry)) {
            if (form.editing >= 0 && form.editing < int(app.config.titles.size())) {
                app.config.titles[size_t(form.editing)] = entry;
            } else {
                app.config.titles.push_back(entry);
            }
            app.SaveConfigOrToast();
            form.Clear();
        }
    }
    if (form.editing >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) form.Clear();
    }
    if (!form.error.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", form.error.c_str());
    }
}

}  // namespace launcher
