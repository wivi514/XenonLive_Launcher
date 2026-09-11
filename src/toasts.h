// Bottom-right notifications: a friend came online, an invitation arrived,
// the connection dropped. Four seconds each, or until acted on for the ones
// with a button.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct Toast {
    std::string text;
    // Seconds on the ImGui clock at which it goes away.
    double expires_at = 0;
    // An optional action, e.g. Accept on an invitation. Clicking it runs the
    // callback and dismisses the toast.
    std::string action_label;
    std::function<void()> action;
};

class Toasts {
public:
    void Push(std::string text, double seconds = 4.0);
    void PushWithAction(std::string text, std::string label, std::function<void()> action,
                        double seconds = 12.0);
    // Draws them and drops the expired ones. Once per frame.
    void Draw();

private:
    std::vector<Toast> toasts_;
};

}  // namespace launcher
