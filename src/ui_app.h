// ui_app.h
// Dear ImGui 界面入口

#ifndef CLIJUDGE_GUI_UI_APP_H
#define CLIJUDGE_GUI_UI_APP_H

#include <string>
#include "imgui.h"

namespace clijudge {
namespace gui {

struct AppOptions {
    std::string dataDir;
    ImFont* font = nullptr;
    float uiScale = 1.0f;
    bool judgeAvailable = true;   // 桌面端 true；Android 端无法本地编译评测
    bool android = false;
};

class UiApp {
public:
    explicit UiApp(AppOptions opts);
    ~UiApp();
    void Frame();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_UI_APP_H
