// The launcher's languages: the six the games ship in.
//
// Every player-facing string goes through T("English text"): the English
// is the key, the table in strings.tsv gives the other languages, and a
// language with no row for a key falls back to English — so a new string
// never breaks a build and a missing translation is visible, not fatal.
// Adding a language is one column in strings.tsv and one line in kLangs.
//
// Format strings keep their %s/%d in the same order in every language.
#pragma once

#include <string>
#include <vector>

namespace xlive::i18n {

enum class Lang { En, Fr, Es, It, Ja, Ko };
inline constexpr int kLangCount = 6;

struct LangInfo {
    Lang lang;
    const char* code;    // "fr" — what launcher.json stores
    const char* native;  // "Français" — what the picker shows
    bool cjk;            // needs a CJK font merged in
};
const LangInfo& Info(Lang lang);
const std::vector<LangInfo>& Langs();

// "fr", "fr-CA", "fr_FR.UTF-8" all map to Fr; unknown is En.
Lang FromCode(const std::string& code);
// The operating system's UI language (LANG on Linux, the user's UI
// language on Windows), for a launcher with nothing chosen yet.
Lang FromSystem();

void Set(Lang lang);
Lang Current();

// The translation of an English string in the current language, or the
// English itself. Pointers stay valid for the run.
const char* T(const char* english);

// A per-machine override: <dir>/<code>.tsv with "key<TAB>translation"
// lines, loaded on top of the built-in table — a translator's draft can
// be tried without a rebuild.
void LoadOverrides(const std::string& dir);

// Every translated string of a language, built-in and overrides, for a
// font atlas that carries exactly the glyphs the launcher draws (all of
// Hangul at three sizes would not fit one texture).
std::vector<std::string> AllTexts(Lang lang);

// A CJK font on this machine for Japanese or Korean, or empty. Selawik
// has no ideographs; the launcher merges this one in for those glyphs.
std::string FindCjkFont(Lang lang);

}  // namespace xlive::i18n
