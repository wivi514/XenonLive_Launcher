// Each screen is a function of the App. They draw inside the content region
// the App has already begun.
#pragma once

#include <string>

#include "xlive/client.h"

namespace launcher {

class App;

// "playing Dead Rising 2: Case West - Navigating the menus (joinable)".
std::string PresenceLine(const xlive::Client::Presence& p);

void DrawSignIn(App& app);
void DrawHome(App& app);
void DrawFriends(App& app);
void DrawInvites(App& app);
void DrawAchievements(App& app);
void DrawProfile(App& app);

}  // namespace launcher
