// Support: a word about who makes this and the ways to help — sponsoring,
// and the public repositories for bug reports and pull requests. The links
// are one table; a new one is one line.
#include <SDL.h>

#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

namespace {

struct Link {
    const char* label;
    const char* url;
    const char* blurb;
};

// Money.
const Link kSponsor[] = {
    {"GitHub Sponsors", "https://github.com/sponsors/wivi514",
     "A monthly or one-time contribution. It pays for the server this launcher talks to."},
};

// Time.
const Link kContribute[] = {
    {"Dead Rising 2: Case Zero", "https://github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp",
     "The port's source. Bug reports and pull requests go here."},
    {"Dead Rising 2: Case West", "https://github.com/wivi514/Dead_Rising_2_Case_West_Xenon_Recomp",
     "The same, for Case West."},
};

void DrawLink(App& app, const Link& link) {
    ImGui::PushID(link.url);
    ImGui::BeginChild("link", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::PushFont(app.fonts.heading);
    ImGui::Text("%s", link.label);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", link.blurb);
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Open", ImVec2(90.0f, 0.0f))) SDL_OpenURL(link.url);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", link.url);
    ImGui::EndChild();
    ImGui::PopID();
}

}  // namespace

void DrawSupport(App& app) {
    ImGui::PushFont(app.fonts.title);
    ImGui::TextColored(xlive::theme::kLime, "Support XenonLive");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(
        "XenonLive is made by one person, in their spare time: the ports, this launcher, the "
        "overlay, and the server that keeps your friends, invitations, messages and achievements. "
        "Running that server costs money every month, and the rest costs evenings. If it has "
        "given you something, here is how to give a little back.");
    ImGui::PopTextWrapPos();

    xlive::theme::Section("Sponsor");
    for (const Link& link : kSponsor) DrawLink(app, link);

    xlive::theme::Section("Contribute");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("A clear bug report is a contribution. So is a fix.");
    ImGui::PopTextWrapPos();
    for (const Link& link : kContribute) DrawLink(app, link);

    ImGui::Spacing();
    ImGui::TextDisabled("Thank you for playing.");
}

}  // namespace launcher
