// gui_config.h
// 原生 GUI 运行配置与数据目录解析
//
// 配置来自可执行文件同目录的 gui.json（不依赖环境变量）:
//   {
//     "data_dir": "",          // 空 = 自动探测，与 clijudge CLI 共享 data/
//     "font_path": "",         // 空 = 自动挑选系统中文字体
//     "window_width": 1280,
//     "window_height": 800,
//     "maximized": false
//   }
//
// 桌面端数据目录探测顺序:
//   1. 显式 data_dir
//   2. 同目录树中的 clijudge 主项目源码 → 用其 data/（即使尚未创建）
//   3. 已存在的数据目录（problems/articles/contests/submissions/langs/config.json）
//   4. 平台默认目录（Android: 应用私有存储；桌面: <exe目录>/data）

#ifndef CLIJUDGE_GUI_CONFIG_H
#define CLIJUDGE_GUI_CONFIG_H

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "json.hpp"
#include "platform.h"

namespace clijudge {
namespace gui {

using json = nlohmann::json;
namespace fs = std::filesystem;

struct GuiConfig {
    std::string dataDir;          // 空 = 自动探测
    std::string fontPath;         // 空 = 自动挑选
    int windowWidth = 1280;
    int windowHeight = 800;
    bool maximized = false;
};

inline std::string guiConfigPath() {
    return platform::pathJoin(platform::exeDir(), "gui.json");
}

inline bool looksLikeDataDir(const std::string& path) {
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return false;
    for (const char* marker : {"problems", "articles", "contests", "submissions", "langs"}) {
        if (fs::exists(fs::path(path) / marker, ec)) return true;
    }
    return fs::exists(fs::path(path) / "config.json", ec);
}

inline GuiConfig loadGuiConfigAt(const std::string& path) {
    GuiConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);  // 记事本另存的 BOM
    }
    if (content.empty()) return cfg;

    try {
        json j = json::parse(content);
        if (!j.is_object()) return cfg;
        if (j.contains("data_dir") && j["data_dir"].is_string())
            cfg.dataDir = j["data_dir"].get<std::string>();
        if (j.contains("font_path") && j["font_path"].is_string())
            cfg.fontPath = j["font_path"].get<std::string>();
        if (j.contains("window_width") && j["window_width"].is_number_integer())
            cfg.windowWidth = j["window_width"].get<int>();
        if (j.contains("window_height") && j["window_height"].is_number_integer())
            cfg.windowHeight = j["window_height"].get<int>();
        if (j.contains("maximized") && j["maximized"].is_boolean())
            cfg.maximized = j["maximized"].get<bool>();
    } catch (...) {
        // 配置损坏时使用默认值，不影响启动
    }
    return cfg;
}

inline GuiConfig loadGuiConfig() {
    return loadGuiConfigAt(guiConfigPath());
}

// 判断目录是否为 clijudge CLI 主项目（避免把 GUI 自身或其父目录误判为 CLI）
inline bool isClijudgeProject(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / "include" / "platform.h", ec) &&
           fs::exists(dir / "include" / "problem.h", ec);
}

inline std::string resolveDataDir(const GuiConfig& cfg, const std::string& platformDefault) {
    fs::path exe = fs::absolute(fs::path(platform::exeDir())).lexically_normal();

    if (!cfg.dataDir.empty()) {
        fs::path p(cfg.dataDir);
        if (p.is_relative()) p = exe / p;
        return p.lexically_normal().string();
    }

    auto ancestors = [&](int limit) {
        std::vector<fs::path> out;
        fs::path cur = exe;
        for (int d = 0; d < limit; ++d) {
            out.push_back(cur);
            fs::path parent = cur.parent_path();
            if (parent.empty() || parent == cur) break;
            cur = parent;
        }
        return out;
    };

    // 2. 主项目源码所在目录优先（数据集单一来源）
    for (const auto& a : ancestors(8)) {
        for (const char* name : {"clijudge", "main"}) {
            fs::path base = a / name;
            if (isClijudgeProject(base)) {
                return (base / "data").lexically_normal().string();
            }
        }
    }

    // 3. 已存在的数据目录（部署场景：只有 data/ 或二进制，没有源码）
    for (const auto& a : ancestors(8)) {
        for (const char* sub : {"data", "clijudge/data", "main/data", "clijudgepp/data"}) {
            fs::path cand = a / sub;
            if (looksLikeDataDir(cand.string())) return cand.lexically_normal().string();
        }
    }

    // 4. 平台默认
    return platformDefault;
}

} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_CONFIG_H
