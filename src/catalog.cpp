#include "catalog.h"

#include <fstream>

#include <cstdlib>
#include <filesystem>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace launcher {

Flavour PlatformFlavour() {
#ifdef _WIN32
    return Flavour::Zip;
#else
    // The AppImage runtime exports APPIMAGE (the file) and APPDIR (its
    // mount). Honoured only when this executable really is inside APPDIR:
    // a terminal that is itself an AppImage exports the same variables to
    // every shell it opens — the same containment test the ports use.
    static const Flavour flavour = [] {
        char exe[4096];
        const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        const std::string path = n > 0 ? std::string(exe, size_t(n)) : std::string();
        const char* appimage = std::getenv("APPIMAGE");
        const char* appdir = std::getenv("APPDIR");
        if (appimage && *appimage && appdir && *appdir && !path.empty() &&
            path.rfind(std::string(appdir), 0) == 0) {
            return Flavour::AppImage;
        }
        // The Steam Deck tarball marks itself with a file beside the binary.
        if (!path.empty()) {
            std::ifstream marker(std::filesystem::path(path).parent_path() / ".flavour");
            std::string word;
            if (marker && std::getline(marker, word) && word.rfind("steamdeck", 0) == 0) {
                return Flavour::SteamDeck;
            }
        }
        return Flavour::Tar;
    }();
    return flavour;
#endif
}

const char* FlavourName(Flavour flavour) {
    switch (flavour) {
        case Flavour::Zip:      return "zip";
        case Flavour::AppImage: return "AppImage";
        case Flavour::Tar:      return "tar.zst";
        case Flavour::SteamDeck: return "Steam Deck tar.gz";
    }
    return "?";
}

const std::vector<CatalogGame>& Catalog() {
    static const std::vector<CatalogGame> games = {
        {"case_zero", 0x58410A8D, "Dead Rising 2: Case Zero",
         "wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp", "CZ", "cz_runtime", "CaseZeroRecomp"},
        {"case_west", 0x58410B00, "Dead Rising 2: Case West",
         "wivi514/Dead_Rising_2_Case_West_Xenon_Recomp", "CW", "cw_runtime", "CaseWestRecomp"},
    };
    return games;
}

const CatalogGame* CatalogByKey(std::string_view key) {
    for (const CatalogGame& game : Catalog()) {
        if (key == game.key) return &game;
    }
    return nullptr;
}

const CatalogGame& LauncherSelf() {
    static const CatalogGame self{"launcher", 0, "XenonLive Launcher",
                                  "wivi514/XenonLive_Launcher", "XL", "xenonlive_launcher",
                                  "XenonLiveLauncher"};
    return self;
}

const char* LauncherVersion() {
#ifdef XENONLIVE_VERSION
    return XENONLIVE_VERSION;
#else
    return "dev";
#endif
}

const CatalogGame* CatalogByTitle(uint32_t title_id) {
    for (const CatalogGame& game : Catalog()) {
        if (game.title_id == title_id) return &game;
    }
    return nullptr;
}

std::string PlatformAssetName(const CatalogGame& game) {
    switch (PlatformFlavour()) {
        case Flavour::Zip:      return std::string(game.bundle) + "-windows-x86_64.zip";
        case Flavour::AppImage: return std::string(game.bundle) + "-linux-x86_64.AppImage";
        case Flavour::Tar:      return std::string(game.bundle) + "-linux-x86_64.tar.zst";
        case Flavour::SteamDeck: return std::string(game.bundle) + "-steamdeck-x86_64.tar.gz";
    }
    return {};
}

std::string PlatformExecutable(const CatalogGame& game) {
    switch (PlatformFlavour()) {
        case Flavour::Zip:      return std::string(game.runtime) + ".exe";
        case Flavour::AppImage: return PlatformAssetName(game);
        case Flavour::Tar:      return game.runtime;
        case Flavour::SteamDeck: return game.runtime;
    }
    return {};
}

}  // namespace launcher
