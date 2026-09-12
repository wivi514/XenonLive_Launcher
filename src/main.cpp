// XenonLive Launcher: an SDL2 window, an SDL_Renderer, an ImGui frame loop,
// and one xlive::Client in launcher mode.
//
// SDL_Renderer rather than a GPU API on purpose: nothing here needs one, and
// it is the backend with the fewest ways to fail on a stranger's machine.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "app.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "theme.h"

namespace {

}  // namespace

// Two development hooks, for driving the launcher from a shell with no one
// at the screen: XENONLIVE_SCREENSHOT=file.bmp saves the window after two
// seconds and keeps going; XENONLIVE_TAB=home|friends|messages|invites|achievements|issues|support
// picks the starting tab; XENONLIVE_PLAY=1 presses Play on the first title
// once signed in; XENONLIVE_ACCEPT=1 presses Accept on the first invitation
// in the inbox; XENONLIVE_INSTALL=<catalog key> presses Install on that game;
// XENONLIVE_SCREENSHOT_MS moves the screenshot later than two seconds;
// XENONLIVE_SWITCH=<xuid hex> presses Use on that saved account;
// XENONLIVE_PROFILE=<gamertag> opens that friend's profile page and presses
// Compare on the first title; XENONLIVE_MESSAGE=<gamertag> opens the
// conversation with that friend, and XENONLIVE_SAY=<text> then sends that.
// XENONLIVE_SIGNIN=register|forgot opens that form; XENONLIVE_FORGOT=<gamertag>
// asks for a recovery code; XENONLIVE_RECOVER=<gamertag>:<code>:<password>
// sets a new password with a code already mailed. XENONLIVE_CAPTURE=<n>
// selects the n-th capture on the Issues tab; XENONLIVE_ISSUE_SEND="title|what
// happened|steps" fills the form and sends it; XENONLIVE_ISSUE_SEARCH=<words>
// searches the reports and selects the first hit.
// None does anything unless set.
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
    if (name == "messages") return launcher::Tab::Messages;
    if (name == "invites") return launcher::Tab::Invites;
    if (name == "achievements") return launcher::Tab::Achievements;
    if (name == "issues") return launcher::Tab::Issues;
    if (name == "support") return launcher::Tab::Support;
    return launcher::Tab::Home;
}

int main(int, char**) {
#ifdef _WIN32
    // A windowed executable has no console, so the log goes to a file the
    // player can attach to a report.
    {
        const std::filesystem::path log = launcher::DataDir() / "launcher" / "launcher.log";
        std::error_code ec;
        std::filesystem::create_directories(log.parent_path(), ec);
        std::freopen(log.string().c_str(), "w", stderr);
    }
#endif
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
    xlive::theme::Apply(ImGui::GetStyle());

    launcher::Installer::GlobalInit();
    launcher::App app;
    std::string error;
    if (!app.Init(error)) {
        std::fprintf(stderr, "[launcher] %s\n", error.c_str());
        return 1;
    }
    app.fonts = xlive::theme::LoadFonts(io, app.config.font_size, app.config.font_path.c_str());
    app.images.Open(renderer, launcher::DataDir() / "launcher");
    app.images.set_server(app.config.server);
    app.tab = StartingTab();
    const char* screenshot = std::getenv("XENONLIVE_SCREENSHOT");
    bool play = std::getenv("XENONLIVE_PLAY") != nullptr;
    bool accept = std::getenv("XENONLIVE_ACCEPT") != nullptr;
    const char* screenshot_ms = std::getenv("XENONLIVE_SCREENSHOT_MS");
    const Uint32 screenshot_at =
        SDL_GetTicks() + (screenshot_ms ? Uint32(std::strtoul(screenshot_ms, nullptr, 10)) : 2000u);
    const char* install_key = std::getenv("XENONLIVE_INSTALL");
    const char* switch_xuid = std::getenv("XENONLIVE_SWITCH");
    const char* profile_tag = std::getenv("XENONLIVE_PROFILE");
    bool profile_compare = false;
    const char* message_tag = std::getenv("XENONLIVE_MESSAGE");
    const char* say = std::getenv("XENONLIVE_SAY");
    if (const char* form = std::getenv("XENONLIVE_SIGNIN")) {
        if (std::string(form) == "register") app.signin.mode = launcher::App::SignInState::Mode::Register;
        if (std::string(form) == "forgot") app.signin.mode = launcher::App::SignInState::Mode::Forgot;
    }
    const char* forgot_tag = std::getenv("XENONLIVE_FORGOT");
    const char* capture_index = std::getenv("XENONLIVE_CAPTURE");
    const char* issue_send = std::getenv("XENONLIVE_ISSUE_SEND");
    const char* issue_search = std::getenv("XENONLIVE_ISSUE_SEARCH");
    bool issue_select_hit = false;
    const char* recover = std::getenv("XENONLIVE_RECOVER");

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
        if (switch_xuid && app.client) {
            std::string switch_error;
            if (!app.SwitchAccount(std::strtoull(switch_xuid, nullptr, 16), switch_error)) {
                std::fprintf(stderr, "[launcher] XENONLIVE_SWITCH: %s\n", switch_error.c_str());
            }
            switch_xuid = nullptr;
        }
        if (forgot_tag && app.client) {
            app.signin.mode = launcher::App::SignInState::Mode::Forgot;
            std::snprintf(app.signin.gamertag, sizeof(app.signin.gamertag), "%s", forgot_tag);
            app.signin.pending = launcher::App::SignInState::Pending::Forgot;
            app.signin.ticket = app.client->ForgotPassword(forgot_tag);
            forgot_tag = nullptr;
        }
        if (recover && app.client) {
            const std::string spec(recover);
            const auto a = spec.find(':');
            const auto b = a == std::string::npos ? a : spec.find(':', a + 1);
            if (b != std::string::npos) {
                app.signin.mode = launcher::App::SignInState::Mode::Forgot;
                app.signin.sent_to = "your email";
                std::snprintf(app.signin.gamertag, sizeof(app.signin.gamertag), "%s",
                              spec.substr(0, a).c_str());
                std::snprintf(app.signin.code, sizeof(app.signin.code), "%s",
                              spec.substr(a + 1, b - a - 1).c_str());
                app.signin.pending = launcher::App::SignInState::Pending::Reset;
                app.signin.ticket = app.client->ResetPassword(spec.substr(0, a),
                                                              spec.substr(a + 1, b - a - 1),
                                                              spec.substr(b + 1));
            } else {
                std::fprintf(stderr, "[launcher] XENONLIVE_RECOVER wants gamertag:code:password\n");
            }
            recover = nullptr;
        }
        if (capture_index && app.issues.scanned) {
            app.tab = launcher::Tab::Issues;
            app.SelectCapture(int(std::strtol(capture_index, nullptr, 10)));
            capture_index = nullptr;
        }
        if (issue_send && !capture_index && app.issues.selected_capture >= 0 && app.signed_in() &&
            app.client->online()) {
            const std::string spec(issue_send);
            const auto a = spec.find('|');
            const auto b = a == std::string::npos ? a : spec.find('|', a + 1);
            std::snprintf(app.issues.title, sizeof(app.issues.title), "%s", spec.substr(0, a).c_str());
            if (a != std::string::npos) {
                std::snprintf(app.issues.summary, sizeof(app.issues.summary), "%s",
                              spec.substr(a + 1, b == std::string::npos ? b : b - a - 1).c_str());
            }
            if (b != std::string::npos) {
                std::snprintf(app.issues.steps, sizeof(app.issues.steps), "%s", spec.substr(b + 1).c_str());
            }
            app.SendCapture();
            issue_send = nullptr;
        }
        if (issue_search && app.signed_in() && app.client->online() && app.issues.search_ticket == 0) {
            app.tab = launcher::Tab::Issues;
            std::snprintf(app.issues.query, sizeof(app.issues.query), "%s", issue_search);
            app.SearchIssues();
            issue_search = nullptr;
            issue_select_hit = true;
        }
        if (issue_select_hit && app.issues.search_ticket == 0 && app.issues.searched) {
            if (!app.issues.results.empty()) {
                app.issues.selected_report = 0;
                app.issues.selected_capture = -1;
            }
            issue_select_hit = false;
        }
        if (install_key && app.signed_in()) {
            if (const launcher::CatalogGame* game = launcher::CatalogByKey(install_key)) {
                app.InstallGame(*game);
            } else {
                std::fprintf(stderr, "[launcher] XENONLIVE_INSTALL: no such game %s\n", install_key);
            }
            install_key = nullptr;
        }
        if (profile_tag && app.signed_in() && app.client->online()) {
            for (const auto& f : app.client->friends()) {
                if (f.gamertag != profile_tag) continue;
                app.OpenProfile(f.xuid, f.gamertag);
                profile_tag = nullptr;
                profile_compare = true;
                break;
            }
        }
        if (message_tag && app.signed_in() && app.client->online()) {
            for (const auto& f : app.client->friends()) {
                if (f.gamertag != message_tag) continue;
                app.OpenConversation(f.xuid, f.gamertag);
                message_tag = nullptr;
                break;
            }
        }
        if (say && !message_tag && app.messages.peer != 0 && app.messages.conversation_loaded) {
            std::snprintf(app.messages.draft, sizeof(app.messages.draft), "%s", say);
            app.SendDraft();
            say = nullptr;
        }
        if (profile_compare && app.profile.card_loaded && app.profile.compare_title == 0 &&
            !app.profile.card.card.titles.empty()) {
            app.LoadCompare(app.profile.card.card.titles.front().title_id);
            profile_compare = false;
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
        SDL_SetRenderDrawColor(renderer, 19, 21, 23, 255);
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
