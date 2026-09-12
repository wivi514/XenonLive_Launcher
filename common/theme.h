// The look shared by the launcher and the in-game overlay: dark grey and
// black with the 360's green, set in Selawik (an open face with Segoe UI's
// metrics, which the 360 dashboard used). Both products link this file and
// the font beside it, so a player sees one thing in the launcher and the
// same thing over the game.
#pragma once

#include "imgui.h"

namespace xlive::theme {

// -- the palette -----------------------------------------------------------
// Charcoal, not pure black, for the ground; panels a step lighter; and two
// greens: the deep one for buttons and the selected blade, the lime for
// text that should be seen first — a heading, "online", an unlocked
// achievement.
inline constexpr ImVec4 kBg      = ImVec4(0.075f, 0.082f, 0.090f, 1.0f);
inline constexpr ImVec4 kRail    = ImVec4(0.055f, 0.060f, 0.066f, 1.0f);
inline constexpr ImVec4 kPanel   = ImVec4(0.115f, 0.125f, 0.135f, 1.0f);
inline constexpr ImVec4 kPanelHi = ImVec4(0.150f, 0.162f, 0.175f, 1.0f);
inline constexpr ImVec4 kBorder  = ImVec4(0.200f, 0.215f, 0.230f, 1.0f);
inline constexpr ImVec4 kGreen   = ImVec4(0.235f, 0.545f, 0.165f, 1.0f);  // #3C8B2A
inline constexpr ImVec4 kGreenHi = ImVec4(0.310f, 0.660f, 0.220f, 1.0f);  // #4FA838
inline constexpr ImVec4 kLime    = ImVec4(0.560f, 0.800f, 0.260f, 1.0f);  // #8FCC42
inline constexpr ImVec4 kText    = ImVec4(0.905f, 0.915f, 0.900f, 1.0f);
inline constexpr ImVec4 kMuted   = ImVec4(0.560f, 0.590f, 0.610f, 1.0f);
inline constexpr ImVec4 kAmber   = ImVec4(0.900f, 0.700f, 0.350f, 1.0f);
inline constexpr ImVec4 kRed     = ImVec4(0.900f, 0.400f, 0.400f, 1.0f);

// Sets every colour and metric. `scale` multiplies the paddings and
// roundings for a UI drawn over a 1080p game rather than in a 960-wide
// window; `bg_alpha` lets the overlay's panel show the game through.
void Apply(ImGuiStyle& style, float scale = 1.0f, float bg_alpha = 1.0f);

// The three sizes of the one face. `body` is what everything is set in;
// `heading` a semibold step up, for a name or a section; `title` the
// wordmark. An empty or unreadable `override_path` means Selawik.
struct Fonts {
    ImFont* body = nullptr;
    ImFont* heading = nullptr;
    ImFont* title = nullptr;
};
Fonts LoadFonts(ImGuiIO& io, float body_size, const char* override_path = nullptr);

// -- widgets in the house style ----------------------------------------------

// A section heading: the label in lime, a rule after it. Replaces
// SeparatorText.
void Section(const char* label);

// One entry of the launcher's rail: full width, the selected one a green
// blade with a lime edge. `badge` is drawn right-aligned ("3" for three
// invitations). Returns true when clicked.
bool RailItem(const char* label, bool selected, const char* badge = nullptr);

// The gamercard tile: gamertag in the heading face over a gamerscore line,
// with a status dot. `status` is "online", "offline", or a longer line.
// Returns true when clicked; the rail opens the account screen on it.
bool Gamercard(const char* gamertag, unsigned gamerscore, bool online, const char* status,
               float width = 0.0f);

// A gloss over the last item: a translucent highlight on its upper half,
// the 360's plastic look, drawn after Button() and the like.
void GlossLastItem();

}  // namespace xlive::theme
