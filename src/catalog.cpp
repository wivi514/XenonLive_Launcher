#include "catalog.h"

namespace launcher {

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

const CatalogGame* CatalogByTitle(uint32_t title_id) {
    for (const CatalogGame& game : Catalog()) {
        if (game.title_id == title_id) return &game;
    }
    return nullptr;
}

std::string PlatformAssetName(const CatalogGame& game) {
#ifdef _WIN32
    return std::string(game.bundle) + "-windows-x86_64.zip";
#else
    return std::string(game.bundle) + "-linux-x86_64.AppImage";
#endif
}

std::string PlatformExecutable(const CatalogGame& game) {
#ifdef _WIN32
    return std::string(game.runtime) + ".exe";
#else
    return PlatformAssetName(game);
#endif
}

}  // namespace launcher
