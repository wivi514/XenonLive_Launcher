#include "i18n.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace xlive::i18n {

// Generated from strings.tsv by tools/gen_strings.py.
struct Row {
    const char* key;
    const char* text[kLangCount - 1];  // fr, es, it, ja, ko; nullptr = none
};
extern const Row kRows[];
extern const size_t kRowCount;

namespace {

const std::vector<LangInfo> g_langs = {
    {Lang::En, "en", "English", false},
    {Lang::Fr, "fr", "Français", false},
    {Lang::Es, "es", "Español", false},
    {Lang::It, "it", "Italiano", false},
    {Lang::Ja, "ja", "日本語", true},
    {Lang::Ko, "ko", "한국어", true},
};

Lang g_current = Lang::En;
// Built once: key -> row. The overrides map wins when it has the key.
std::unordered_map<std::string_view, const Row*>* g_index = nullptr;
std::unordered_map<std::string, std::string> g_overrides[kLangCount];
std::mutex g_mutex;

void BuildIndex() {
    if (g_index) return;
    g_index = new std::unordered_map<std::string_view, const Row*>();
    g_index->reserve(kRowCount * 2);
    for (size_t i = 0; i < kRowCount; ++i) (*g_index)[kRows[i].key] = &kRows[i];
}

}  // namespace

const LangInfo& Info(Lang lang) { return g_langs[size_t(lang)]; }
const std::vector<LangInfo>& Langs() { return g_langs; }

Lang FromCode(const std::string& code) {
    std::string two;
    for (char c : code) {
        if (c == '-' || c == '_' || c == '.') break;
        two += char(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const LangInfo& info : g_langs) {
        if (two == info.code) return info.lang;
    }
    return Lang::En;
}

Lang FromSystem() {
#ifdef _WIN32
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string code;
        for (wchar_t* p = name; *p && *p != L'-'; ++p) code += char(*p);
        return FromCode(code);
    }
    return Lang::En;
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const char* v = std::getenv(var); v && *v && std::strcmp(v, "C") != 0) return FromCode(v);
    }
    return Lang::En;
#endif
}

void Set(Lang lang) { g_current = lang; }
Lang Current() { return g_current; }

const char* T(const char* english) {
    if (g_current == Lang::En || !english) return english;
    const int slot = int(g_current) - 1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto& over = g_overrides[int(g_current)];
        if (!over.empty()) {
            if (auto it = over.find(english); it != over.end()) return it->second.c_str();
        }
    }
    BuildIndex();
    if (auto it = g_index->find(english); it != g_index->end()) {
        const char* t = it->second->text[slot];
        if (t && *t) return t;
    }
    return english;
}

void LoadOverrides(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const LangInfo& info : g_langs) {
        if (info.lang == Lang::En) continue;
        std::ifstream in(std::filesystem::path(dir) / (std::string(info.code) + ".tsv"));
        if (!in) continue;
        auto& table = g_overrides[int(info.lang)];
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto tab = line.find('\t');
            if (tab == std::string::npos || line.empty() || line[0] == '#') continue;
            std::string key = line.substr(0, tab), value = line.substr(tab + 1);
            // "\n" in the file is a newline in the string.
            for (std::string* s : {&key, &value}) {
                for (size_t at; (at = s->find("\\n")) != std::string::npos;) s->replace(at, 2, "\n");
            }
            table[key] = value;
        }
    }
}

std::vector<std::string> AllTexts(Lang lang) {
    std::vector<std::string> out;
    if (lang == Lang::En) return out;
    const int slot = int(lang) - 1;
    for (size_t i = 0; i < kRowCount; ++i) {
        if (kRows[i].text[slot]) out.emplace_back(kRows[i].text[slot]);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [key, value] : g_overrides[int(lang)]) out.push_back(value);
    out.push_back(Info(lang).native);
    return out;
}

std::string FindCjkFont(Lang lang) {
    if (!Info(lang).cjk) return {};
    const bool ja = lang == Lang::Ja;
    std::vector<std::string> candidates;
#ifdef _WIN32
    char windir[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(windir, MAX_PATH);
    const std::string fonts = std::string(windir) + "\\Fonts\\";
    if (ja) candidates = {fonts + "YuGothM.ttc", fonts + "meiryo.ttc", fonts + "msgothic.ttc"};
    else candidates = {fonts + "malgun.ttf", fonts + "malgunsl.ttf", fonts + "gulim.ttc"};
    candidates.push_back(fonts + "msyh.ttc");
#else
    const char* dirs[] = {"/usr/share/fonts", "/usr/local/share/fonts", "/run/host/usr/share/fonts"};
    // In order of preference: a font made for the language first, the
    // pan-CJK families next, the fallbacks last.
    const char* names[] = {
        ja ? "NotoSansJP-Regular.ttf" : "NotoSansKR-Regular.ttf",
        ja ? "NotoSansJP-Regular.otf" : "NotoSansKR-Regular.otf",
        ja ? "NotoSansCJKjp-Regular.otf" : "NotoSansCJKkr-Regular.otf",
        // Not the "-VF" variable builds: their CFF2 outlines are beyond
        // the font loader (stb_truetype), which asserts on them.
        "NotoSansCJK-Regular.ttc",
        ja ? "ipag.ttf" : "NanumGothic.ttf",
        "wqy-microhei.ttc", "DroidSansFallbackFull.ttf", "DroidSansFallback.ttf",
    };
    std::vector<std::string> found[sizeof(names) / sizeof(names[0])];
    std::error_code ec;
    for (const char* dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string name = entry.path().filename().string();
            for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
                if (name == names[i]) found[i].push_back(entry.path().string());
            }
        }
    }
    for (const auto& paths : found) candidates.insert(candidates.end(), paths.begin(), paths.end());
    if (const char* home = std::getenv("HOME")) {
        // Steam's own fonts, present on every Deck and most Steam installs.
        candidates.push_back(std::string(home) + "/.steam/steam/clientui/fonts/NotoSansCJK-Regular.ttc");
        candidates.push_back(std::string(home) + "/.local/share/Steam/clientui/fonts/NotoSansCJK-Regular.ttc");
    }
#endif
    std::error_code ec2;
    for (const std::string& path : candidates) {
        if (std::filesystem::is_regular_file(path, ec2)) return path;
    }
    return {};
}

}  // namespace xlive::i18n
