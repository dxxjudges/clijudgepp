// i18n_online.cpp
// 在线语言包：从仓库的 langs 分支拉取（GitHub Contents API + raw 下载）
//
//   https://api.github.com/repos/dxxjudges/clijudgepp/contents?ref=langs
//   https://raw.githubusercontent.com/dxxjudges/clijudgepp/langs/<code>.cjl
//
// 语言包放在独立分支而不是主分支，新增/修订语言包不需要重新发版；
// HTTP 复用 clijudge CLI 的 httpGet（Windows WinHTTP / POSIX + OpenSSL）。

#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "json.hpp"

// Windows: platform.h 先引入 windows.h，否则 lang.h 里的 winhttp.h 无法编译
#include "platform.h"
#include "lang.h"

namespace clijudge {
namespace gui {
namespace i18n {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

const char* kLangsOwner = "dxxjudges";
const char* kLangsRepo = "clijudgepp";
const char* kLangsBranch = "langs";

bool validCode(const std::string& code) {
    if (code.empty() || code.size() > 64) return false;
    if (code == "." || code == "..") return false;
    for (char c : code) {
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

std::string onlineListUrl() {
    return std::string("https://api.github.com/repos/") + kLangsOwner + "/" + kLangsRepo +
           "/contents?ref=" + kLangsBranch;
}

std::string onlineRawUrl(const std::string& fileName) {
    return std::string("https://raw.githubusercontent.com/") + kLangsOwner + "/" + kLangsRepo +
           "/" + kLangsBranch + "/" + fileName;
}

} // namespace

std::vector<std::string> fetchOnlineLanguages() {
    std::vector<std::string> out;
    std::string body = clijudge::lang::httpGet(onlineListUrl());
    if (body.empty()) return out;
    try {
        json list = json::parse(body);
        if (!list.is_array()) return out;
        for (const auto& item : list) {
            if (!item.is_object()) continue;
            if (item.value("type", "") != "file") continue;
            std::string name = item.value("name", "");
            if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".cjl") != 0) continue;
            out.push_back(name.substr(0, name.size() - 4));
        }
    } catch (...) {
        return std::vector<std::string>();
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool downloadLanguage(const std::string& code) {
    if (!validCode(code)) return false;

    std::string text = clijudge::lang::httpGet(onlineRawUrl(code + ".cjl"));
    // 2 MB 上限：语言包应在几十 KB 量级，避免把错误页/大文件写进数据目录
    if (text.empty() || text.size() > 2u * 1024u * 1024u) return false;

    // 必须是含 meta/strings 的 JSON 对象，否则视为下载失败
    try {
        json j = json::parse(text);
        if (!j.is_object() || !j.contains("meta") || !j["meta"].is_object()) return false;
        if (!j.contains("strings") || !j["strings"].is_object()) return false;
    } catch (...) {
        return false;
    }

    std::string dir = languageDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;
    return platform::writeFileAtomic(platform::pathJoin(dir, code + ".cjl"), text);
}

} // namespace i18n
} // namespace gui
} // namespace clijudge
