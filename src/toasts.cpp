#include "toasts.h"

#include "imgui.h"

namespace launcher {

void Toasts::Push(std::string text, double seconds) {
    Toast toast;
    toast.text = std::move(text);
    toast.expires_at = ImGui::GetTime() + seconds;
    toasts_.push_back(std::move(toast));
    // A burst of them (a friends list snapshot with ten people online) must
    // not cover the screen. The oldest go; they were about to anyway.
    while (toasts_.size() > 6) toasts_.erase(toasts_.begin());
}

void Toasts::PushWithAction(std::string text, std::string label, std::function<void()> action,
                            double seconds) {
    Toast toast;
    toast.text = std::move(text);
    toast.expires_at = ImGui::GetTime() + seconds;
    toast.action_label = std::move(label);
    toast.action = std::move(action);
    toasts_.push_back(std::move(toast));
    while (toasts_.size() > 6) toasts_.erase(toasts_.begin());
}

void Toasts::Draw() {
    const double now = ImGui::GetTime();
    std::erase_if(toasts_, [&](const Toast& t) { return t.expires_at <= now; });
    if (toasts_.empty()) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f;
    float y = viewport->WorkPos.y + viewport->WorkSize.y - pad;

    // Newest at the bottom, stacking upward. Each is its own window so it
    // can carry a button and so the stack never overlaps the screen behind
    // it in a way that eats clicks.
    for (size_t i = toasts_.size(); i-- > 0;) {
        Toast& toast = toasts_[i];
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad, y),
                                ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(240, 0), ImVec2(420, FLT_MAX));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav;
        ImGui::PushID(int(i));
        if (ImGui::Begin("##toast", nullptr, flags)) {
            ImGui::PushTextWrapPos(400.0f);
            ImGui::TextUnformatted(toast.text.c_str());
            ImGui::PopTextWrapPos();
            if (toast.action) {
                if (ImGui::SmallButton(toast.action_label.c_str())) {
                    auto action = std::move(toast.action);
                    toast.expires_at = 0;  // dismissed next frame
                    action();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Dismiss")) toast.expires_at = 0;
            }
            y -= ImGui::GetWindowSize().y + 8.0f;
        }
        ImGui::End();
        ImGui::PopID();
    }
}

}  // namespace launcher
