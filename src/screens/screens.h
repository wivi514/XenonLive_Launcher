// Each screen is a function of the App. They draw inside the content region
// the App has already begun.
#pragma once

namespace launcher {

class App;

void DrawSignIn(App& app);
void DrawHome(App& app);
void DrawFriends(App& app);
void DrawInvites(App& app);
void DrawAchievements(App& app);

}  // namespace launcher
