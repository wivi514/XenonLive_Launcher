// XenonLive Launcher: an SDL2 window, an SDL_Renderer, an ImGui frame loop,
// and one xlive::Client in launcher mode.
//
// SDL_Renderer rather than a GPU API on purpose: nothing here needs one, and
// it is the backend with the fewest ways to fail on a stranger's machine.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace {

void ApplyStyle(ImGuiStyle& style) {
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarSize = 12.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.26f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.36f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.48f, 0.29f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.60f, 0.36f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.19f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] = style.Colors[ImGuiCol_Separator];
}

// ImGui's built-in ProggyClean has no accents or CJK. Gamertags are ASCII
// (server-enforced) and both Dead Rising titles' presence strings are
// English, so v1 is fine with it; launcher.json's font_path is the hook for
// a TTF with more.
void LoadFont(ImGuiIO& io, const launcher::Config& config) {
    if (!config.font_path.empty()) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(config.font_path.c_str(), config.font_size);
        if (font) return;
        std::fprintf(stderr, "[launcher] could not load font %s; using the built-in one\n",
                     config.font_path.c_str());
    }
    io.Fonts->AddFontDefault();
}

}  // namespace

// Two development hooks, for driving the launcher from a shell with no one
// at the screen: XENONLIVE_SCREENSHOT=file.bmp saves the window after two
// seconds and keeps going; XENONLIVE_TAB=home|friends|invites|achievements
// picks the starting tab; XENONLIVE_PLAY=1 presses Play on the first title
// once signed in; XENONLIVE_ACCEPT=1 presses Accept on the first invitation
// in the inbox; XENONLIVE_INSTALL=<catalog key> presses Install on that game;
// XENONLIVE_SCREENSHOT_MS moves the screenshot later than two seconds. None
// does anything unless set.
void SaveScreenshot(SDL_Renderer* renderer, const char* path) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels,
                             surface->pitch) == 0) {
        SDL_SaveBMP(surface, path);
        std::fprintf(stderr, "[launcher] screenshot written to %s\n", path);
    } else {
        std::fprintf(stderr, "[launcher] screenshot failed: %s\n", SDL_GetError());
    }
    SDL_FreeSurface(surface);
}

launcher::Tab StartingTab() {
    const char* tab = std::getenv("XENONLIVE_TAB");
    if (!tab) return launcher::Tab::Home;
    const std::string name(tab);
    if (name == "friends") return launcher::Tab::Friends;
    if (name == "invites") return launcher::Tab::Invites;
    if (name == "achievements") return launcher::Tab::Achievements;
    return launcher::Tab::Home;
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

    SDL_Window* window = SDL_CreateWindow(
        "XenonLive", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 640,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // one fixed layout; nothing to remember
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyStyle(ImGui::GetStyle());

    launcher::Installer::GlobalInit();
    launcher::App app;
    std::string error;
    if (!app.Init(error)) {
        std::fprintf(stderr, "[launcher] %s\n", error.c_str());
        return 1;
    }
    LoadFont(io, app.config);
    app.tab = StartingTab();
    const char* screenshot = std::getenv("XENONLIVE_SCREENSHOT");
    bool play = std::getenv("XENONLIVE_PLAY") != nullptr;
    bool accept = std::getenv("XENONLIVE_ACCEPT") != nullptr;
    const char* screenshot_ms = std::getenv("XENONLIVE_SCREENSHOT_MS");
    const Uint32 screenshot_at =
        SDL_GetTicks() + (screenshot_ms ? Uint32(std::strtoul(screenshot_ms, nullptr, 10)) : 2000u);
    const char* install_key = std::getenv("XENONLIVE_INSTALL");

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    while (!app.quit) {
        SDL_Event event;
        // Block briefly rather than spin: the launcher sits open for hours
        // beside a game, and a toast a few tens of milliseconds late is fine.
        if (SDL_WaitEventTimeout(&event, 50)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) app.quit = true;
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    event.window.windowID == SDL_GetWindowID(window)) {
                    app.quit = true;
                }
            } while (SDL_PollEvent(&event));
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            // Still drain events and tickets so nothing piles up unseen.
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        if (install_key && app.signed_in()) {
            if (const launcher::CatalogGame* game = launcher::CatalogByKey(install_key)) {
                app.InstallGame(*game);
            } else {
                std::fprintf(stderr, "[launcher] XENONLIVE_INSTALL: no such game %s\n", install_key);
            }
            install_key = nullptr;
        }
        if (accept && app.signed_in() && app.client->online()) {
            const auto inbox = app.client->invites();
            if (!inbox.empty()) {
                accept = false;
                app.AcceptInvite(inbox.front());
            }
        }
        if (play && app.signed_in() && app.client->online() && !app.config.titles.empty()) {
            play = false;
            std::string launch_error;
            if (!app.Launch(0, launch_error)) {
                std::fprintf(stderr, "[launcher] XENONLIVE_PLAY: %s\n", launch_error.c_str());
            }
        }
        ImGui::Render();

        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 23, 25, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (screenshot && SDL_GetTicks() >= screenshot_at) {
            SaveScreenshot(renderer, screenshot);
            screenshot = nullptr;
        }
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    launcher::Installer::GlobalCleanup();
    return 0;
}
