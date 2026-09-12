// The games this launcher knows how to install: the XenonRecomp ports with
// public releases on GitHub. The launcher downloads the release for this
// platform; the player supplies their own XBLA package, as the ports' own
// README says, and updates never touch it.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

struct CatalogGame {
    // A stable key for launcher.json and the install directory name.
    const char* key;
    uint32_t title_id;
    const char* name;
    // "owner/repo" on GitHub.
    const char* repo;
    // The port's environment prefix ("CW" for CW_VKDRAW, CW_XLIVE_ONLINE...).
    const char* prefix;
    // The runtime's executable name, without a platform suffix.
    const char* runtime;
    // The bundle name the release assets and the zip's top directory carry.
    const char* bundle;
};

const std::vector<CatalogGame>& Catalog();
const CatalogGame* CatalogByKey(std::string_view key);
const CatalogGame* CatalogByTitle(uint32_t title_id);

// How this launcher was packaged decides how a game is: a launcher running
// as an AppImage installs the game's AppImage (one file beside its own
// assets/), a launcher from the tarball installs the game's .tar.zst, and
// Windows installs the zip.
enum class Flavour { Zip, AppImage, Tar };
Flavour PlatformFlavour();
const char* FlavourName(Flavour flavour);

// The release asset this launcher installs for a game.
std::string PlatformAssetName(const CatalogGame& game);
// The executable an install produces, relative to the install directory.
std::string PlatformExecutable(const CatalogGame& game);

}  // namespace launcher
