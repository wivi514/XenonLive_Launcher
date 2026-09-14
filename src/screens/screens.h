// Each screen is a function of the App. They draw inside the content region
// the App has already begun.
#pragma once

#include <string>

#include "xlive/client.h"

namespace launcher {

class App;

// "playing Dead Rising 2: Case West - Navigating the menus (joinable)".
std::string PresenceLine(const xlive::Client::Presence& p);
// Shows a directory in the desktop's file browser.
void OpenFolder(const std::string& path);

void DrawSignIn(App& app);
void DrawHome(App& app);
void DrawFriends(App& app);
void DrawMessages(App& app);
void DrawInvites(App& app);
void DrawAchievements(App& app);
void DrawProfile(App& app);
void DrawIssues(App& app);
void DrawAccount(App& app);
void DrawSupport(App& app);

}  // namespace launcher
