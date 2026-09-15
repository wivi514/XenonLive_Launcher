// The language picker, drawn on the sign-in screen (a first run in the
// wrong language is the first thing to fix) and on the account screen.
#include "app.h"
#include "imgui.h"
#include "screens/screens.h"

namespace launcher {

void DrawLanguagePicker(App& app, float width) {
    namespace i18n = xlive::i18n;
    const bool system = app.config.language.empty();
    const i18n::Lang chosen = system ? i18n::FromSystem() : i18n::FromCode(app.config.language);
    std::string shown = system ? std::string(T("System language")) + " (" + i18n::Info(chosen).native + ")"
                               : std::string(i18n::Info(chosen).native);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##language", shown.c_str())) {
        if (ImGui::Selectable(T("System language"), system)) {
            app.config.language.clear();
            app.language_changed = true;
        }
        for (const i18n::LangInfo& info : i18n::Langs()) {
            const bool selected = !system && info.lang == chosen;
            if (ImGui::Selectable(info.native, selected)) {
                app.config.language = info.code;
                app.language_changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

}  // namespace launcher
