// gui_config.h
// GUI 运行配置与数据目录解析
//
// 配置全部来自配置文件 <exe目录>/gui.json（不依赖环境变量）:
//   {
//     "data_dir": "",            // 空 = 自动探测，与 clijudge CLI 共享 data/
//     "host": "127.0.0.1",
//     "port": 8787,
//     "open_browser": true
//   }
//
// 数据目录自动探测：从可执行文件所在目录逐层向上，依次检查
//   <祖先>/data、<祖先>/main/data、<祖先>/clijudge/data
// 命中已有数据（problems/articles/contests/submissions/config.json）就采用，
// 因此把 GUI 与 clijudge 主项目放在同一目录时天然共享同一份数据。

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
    std::string dataDir;                 // 空 = 自动探测
    std::string host = "127.0.0.1";
    int port = 8787;
    bool openBrowser = true;
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

inline GuiConfig loadGuiConfig() {
    GuiConfig cfg;
    std::ifstream f(guiConfigPath());
    if (!f.is_open()) return cfg;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);
    }
    if (content.empty()) return cfg;

    try {
        json j = json::parse(content);
        if (!j.is_object()) return cfg;
        if (j.contains("data_dir") && j["data_dir"].is_string()) {
            cfg.dataDir = j["data_dir"].get<std::string>();
        }
        if (j.contains("host") && j["host"].is_string()) {
            cfg.host = j["host"].get<std::string>();
        }
        if (j.contains("port")) {
            if (j["port"].is_number_integer()) cfg.port = j["port"].get<int>();
            else if (j["port"].is_string()) {
                try { cfg.port = std::stoi(j["port"].get<std::string>()); } catch (...) {}
            }
        }
        if (j.contains("open_browser") && j["open_browser"].is_boolean()) {
            cfg.openBrowser = j["open_browser"].get<bool>();
        }
    } catch (...) {
        // 配置损坏时使用默认值，不影响启动
    }
    return cfg;
}

// 判断目录是否为 clijudge 主项目（避免把同名的普通目录当成 CLI）
inline bool isClijudgeProject(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / "include" / "platform.h", ec) ||
           fs::exists(dir / "include" / "problem.h", ec) ||
           fs::exists(dir / "src" / "main.cpp", ec);
}

// 数据目录探测结果:
//   1. 显式配置
//   2. 同一目录树中存在的 clijudge 主项目 → 直接使用其 data/（即使尚未创建，也保证与 CLI 共享）
//   3. 已存在的 CLI 数据目录（部署场景: 只有 data/ 或二进制，没有源码）
//   4. 可执行文件旁的 data/
inline std::string resolveDataDir(const GuiConfig& cfg) {
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

    // 2. 主项目源码所在目录优先（数据结构单一来源）
    for (const auto& a : ancestors(8)) {
        for (const char* name : {"main", "clijudge"}) {
            fs::path base = a / name;
            if (isClijudgeProject(base)) {
                return (base / "data").lexically_normal().string();
            }
        }
    }

    // 3. 已存在的数据目录
    for (const auto& a : ancestors(8)) {
        for (const char* sub : {"data", "main/data", "clijudge/data", "clijudgepp/data"}) {
            fs::path cand = a / sub;
            if (looksLikeDataDir(cand.string())) return cand.lexically_normal().string();
        }
    }

    // 4. 无法判断时使用自身目录
    return (exe / "data").lexically_normal().string();
}

} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_CONFIG_H
