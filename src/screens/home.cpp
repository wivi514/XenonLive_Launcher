// The account card, Sign out, and the Games section: every game the
// launcher knows how to install, its installed version, whether a newer
// release exists, and Play. The launcher downloads the port's release for
// this platform; the player supplies their own XBLA package, and an update
// never touches it.
#include <cstdio>
#include <cstring>

#include <SDL.h>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void OpenFolder(const std::string& path) {
#ifdef _WIN32
    SDL_OpenURL(("file:///" + path).c_str());
#else
    SDL_OpenURL(("file://" + path).c_str());
#endif
}

namespace {

using xlive::theme::kAmber;
using xlive::theme::kLime;
using xlive::theme::kRed;
const ImVec4& kGreen = kLime;

std::string Megabytes(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buf;
}

// The card for one catalog game.
void DrawGame(App& app, const CatalogGame& game) {
    const int index = app.TitleIndexForKey(game.key);
    const TitleEntry* entry = index >= 0 ? &app.config.titles[size_t(index)] : nullptr;
    const InstallProgress progress = app.installer.Poll();
    const bool working = app.installer.busy() && progress.key == game.key;
    const bool other_working = app.installer.busy() && progress.key != game.key;
    const bool running_this = app.game.running() && app.running_title == index && index >= 0;
    const auto latest = app.latest_tags.find(game.key);
    const bool update_available =
        entry && latest != app.latest_tags.end() && latest->second != entry->version;

    ImGui::PushID(game.key);
    ImGui::BeginChild("game", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    // The title's tile, when the server has the title imported.
    if (const Image tile = app.images.Title(game.title_id); tile.texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(tile.texture), ImVec2(64.0f, 64.0f));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(app.fonts.heading);
    ImGui::Text("%s", game.name);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TitleIdHex(game.title_id).c_str());

    // -- the state line ------------------------------------------------------
    if (working) {
        const char* phase = PhaseName(progress.phase);
        if (progress.phase == InstallPhase::Downloading && progress.total > 0) {
            const float fraction = float(double(progress.done) / double(progress.total));
            char overlay[64];
            std::snprintf(overlay, sizeof(overlay), "%s / %s", Megabytes(progress.done).c_str(),
                          Megabytes(progress.total).c_str());
            ImGui::ProgressBar(fraction, ImVec2(-100.0f, 0.0f), overlay);
        } else if (progress.phase == InstallPhase::Installing && progress.total > 0) {
            ImGui::ProgressBar(float(double(progress.done) / double(progress.total)),
                               ImVec2(-100.0f, 0.0f), "unpacking");
        } else {
            ImGui::ProgressBar(-1.0f * float(ImGui::GetTime()), ImVec2(-100.0f, 0.0f), phase);
        }
        ImGui::SameLine();
        if (xlive::theme::SmallSecondaryButton("Cancel")) app.installer.Cancel();
    } else if (!entry) {
        ImGui::TextDisabled("not installed");
        if (latest != app.latest_tags.end()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- latest is %s", latest->second.c_str());
        }
    } else {
        ImGui::TextColored(kGreen, "installed %s", entry->version.c_str());
        if (update_available) {
            ImGui::SameLine();
            ImGui::TextColored(kAmber, "- %s is available", latest->second.c_str());
        } else if (latest != app.latest_tags.end()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- up to date");
        }
    }
    const auto error = app.install_errors.find(game.key);
    if (error != app.install_errors.end() && !working) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kRed, "%s", error->second.c_str());
        ImGui::PopTextWrapPos();
    }

    // -- where the player's game goes ------------------------------------------
    if (entry) {
        const std::string state = app.PackageState(*entry);
        const std::string package_dir =
            (std::filesystem::path(entry->cwd) / "assets" / "package").string();
        if (state == "ready") {
            ImGui::TextDisabled("your game data is installed");
        } else if (state == "package found") {
            ImGui::TextDisabled("your package is in place; the first Play unpacks it");
        } else {
            ImGui::TextColored(kAmber, "put your XBLA package (title %s) in:",
                               TitleIdHex(game.title_id).c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", package_dir.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TextDisabled("or drop it onto the game's own window after pressing Play");
        }
    }

    // -- the buttons ------------------------------------------------------------
    ImGui::BeginDisabled(working || other_working);
    if (!entry) {
        if (ImGui::Button("Install", ImVec2(90.0f, 0.0f))) app.InstallGame(game);
    } else if (running_this) {
        ImGui::TextColored(kGreen, "running (pid %lld)", (long long)app.game.pid());
    } else {
        ImGui::BeginDisabled(app.game.running());
        if (ImGui::Button("Play", ImVec2(90.0f, 0.0f))) {
            std::string launch_error;
            if (!app.Launch(index, launch_error)) {
                app.toasts.Push("Could not launch: " + launch_error, 8.0);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (update_available) {
            if (ImGui::Button(("Update to " + latest->second).c_str())) app.InstallGame(game);
        } else {
            if (xlive::theme::SmallSecondaryButton("Reinstall")) app.InstallGame(game);
        }
        ImGui::SameLine();
        if (xlive::theme::SmallSecondaryButton("Folder")) OpenFolder(entry->cwd);
    }
    const auto page = app.release_pages.find(game.key);
    if (page != app.release_pages.end() && !page->second.empty()) {
        ImGui::SameLine();
        if (xlive::theme::SmallSecondaryButton("Release notes")) SDL_OpenURL(page->second.c_str());
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopID();
}

}  // namespace

void DrawHome(App& app) {
    // -- a newer launcher ------------------------------------------------
    // One banner, above everything, while GitHub's latest is not what this
    // binary was built as. Update downloads it beside this one, verifies
    // it, swaps it in and restarts.
    if (!app.launcher_update.empty() || app.launcher_updating()) {
        const InstallProgress progress = app.installer.Poll();
        const bool working = app.launcher_updating();
        ImGui::PushStyleColor(ImGuiCol_Border, kAmber);
        ImGui::BeginChild("launcher_update", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        ImGui::PushFont(app.fonts.heading);
        ImGui::TextColored(kAmber, "XenonLive Launcher %s is available", app.launcher_update.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("you have %s", LauncherVersion());
        if (working) {
            const char* phase = PhaseName(progress.phase);
            if (progress.phase == InstallPhase::Downloading && progress.total > 0) {
                char overlay[64];
                std::snprintf(overlay, sizeof(overlay), "%s / %s", Megabytes(progress.done).c_str(),
                              Megabytes(progress.total).c_str());
                ImGui::ProgressBar(float(double(progress.done) / double(progress.total)),
                                   ImVec2(-1.0f, 0.0f), overlay);
            } else {
                ImGui::ProgressBar(-1.0f * float(ImGui::GetTime()), ImVec2(-1.0f, 0.0f), phase);
            }
            ImGui::TextDisabled("The launcher restarts by itself when this is done.");
        } else {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Downloaded beside this one and checked against the release's "
                                "SHA256SUMS, then the launcher restarts as the new version.");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Update and restart")) app.UpdateLauncher();
            const auto page = app.release_pages.find(LauncherSelf().key);
            if (page != app.release_pages.end() && !page->second.empty()) {
                ImGui::SameLine();
                if (xlive::theme::SmallSecondaryButton("Release notes")) SDL_OpenURL(page->second.c_str());
            }
            if (!app.launcher_update_error.empty()) {
                ImGui::TextColored(kRed, "%s", app.launcher_update_error.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
    }

    // -- the account card ------------------------------------------------
    const xlive::Identity id = app.client->identity();
    ImGui::BeginChild("account", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::PushFont(app.fonts.heading);
    ImGui::Text("%s", id.gamertag.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("  %u G", id.gamerscore);
    ImGui::TextDisabled("%s", app.client->status().c_str());
    ImGui::TextDisabled("%s", app.client->gateway_connected() ? "live updates on"
                                                               : "live updates off");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 262.0f);
    if (xlive::theme::SmallSecondaryButton("Account")) app.OpenAccount();
    ImGui::SameLine();
    if (xlive::theme::SmallSecondaryButton("Switch account")) ImGui::OpenPopup("switch");
    ImGui::SameLine();
    if (xlive::theme::SmallSecondaryButton("Sign out")) app.SignOut();
    if (ImGui::BeginPopup("switch")) {
        for (const SavedAccount& account : app.accounts.list()) {
            if (account.xuid == id.xuid) continue;
            const std::string label =
                account.gamertag + (account.has_tokens() ? "" : "  (needs password)");
            if (ImGui::Selectable(label.c_str())) {
                if (account.has_tokens()) {
                    std::string error;
                    if (!app.SwitchAccount(account.xuid, error)) app.toasts.Push(error, 6.0);
                } else {
                    std::snprintf(app.signin.gamertag, sizeof(app.signin.gamertag), "%s",
                                  account.gamertag.c_str());
                    app.adding_account = true;
                }
            }
        }
        if (app.accounts.list().size() > 1) ImGui::Separator();
        if (ImGui::Selectable("Add another account...")) {
            app.signin.gamertag[0] = '\0';
            app.adding_account = true;
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    if (!app.config_error.empty()) {
        ImGui::TextColored(kAmber, "%s", app.config_error.c_str());
    }

    // -- games ------------------------------------------------------------
    ImGui::Spacing();
    xlive::theme::Section("Games");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Releases come from GitHub as %s builds (this launcher's own kind), checked "
                        "against the release's SHA256SUMS. You supply your own copy of each game.",
                        FlavourName(PlatformFlavour()));
    ImGui::PopTextWrapPos();
    // The release check runs by itself: when the launcher starts, then
    // every five minutes. This line says where it is.
    if (app.release_check_running()) {
        ImGui::TextDisabled("checking for new releases...");
    } else if (!app.release_check_error.empty()) {
        ImGui::TextColored(kAmber, "could not check for releases: %s", app.release_check_error.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("- tried again every 5 minutes");
    } else if (app.last_release_check >= 0.0) {
        const int ago = int((ImGui::GetTime() - app.last_release_check) / 60.0);
        if (ago < 1) ImGui::TextDisabled("releases checked just now - again in 5 minutes");
        else ImGui::TextDisabled("releases checked %d min ago - again every 5 minutes", ago);
    }
    for (const CatalogGame& game : Catalog()) DrawGame(app, game);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("installed under %s",
                        (app.config.games_dir.empty() ? DefaultGamesDir()
                                                      : std::filesystem::path(app.config.games_dir))
                            .string()
                            .c_str());
    ImGui::PopTextWrapPos();

    // -- builds pointed at by hand ----------------------------------------------
    bool any_custom = false;
    for (const TitleEntry& entry : app.config.titles) any_custom |= !entry.managed();
    if (!any_custom) return;
    ImGui::Spacing();
    xlive::theme::Section("Builds from launcher.json");
    for (int i = 0; i < int(app.config.titles.size()); ++i) {
        const TitleEntry& entry = app.config.titles[size_t(i)];
        if (entry.managed()) continue;
        ImGui::PushID(i);
        ImGui::BeginChild("custom", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushFont(app.fonts.heading);
        ImGui::Text("%s", entry.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TitleIdHex(entry.title_id).c_str());
        ImGui::TextDisabled("%s", entry.exe.c_str());
        if (app.game.running() && app.running_title == i) {
            ImGui::TextColored(kGreen, "running (pid %lld)", (long long)app.game.pid());
        } else {
            ImGui::BeginDisabled(app.game.running() || entry.exe.empty());
            if (ImGui::Button("Play", ImVec2(90.0f, 0.0f))) {
                std::string launch_error;
                if (!app.Launch(i, launch_error)) {
                    app.toasts.Push("Could not launch: " + launch_error, 8.0);
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

}  // namespace launcher
