// i18n.cpp
// 显示语言包实现（.cjl 读取、内置英文目录、切换与持久化）

#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "json.hpp"
#include "platform.h"

namespace clijudge {
namespace gui {
namespace i18n {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct State {
    mutable std::mutex mu;
    std::string dataDir;
    std::string configPath;
    std::string lang = "zh";
    std::vector<LangInfo> langs;
    std::unordered_map<std::string, std::string> active;
    // 已解析文案缓存（键含语言，永不删除：t() 返回的指针需长期有效）
    std::unordered_map<std::string, std::string> cache;
};

State& state() {
    static State s;
    return s;
}

bool validCode(const std::string& code) {
    if (code.empty() || code.size() > 64) return false;
    if (code == "." || code == "..") return false;
    for (char c : code) {
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

std::string stripBom(const std::string& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        return s.substr(3);
    return s;
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    out = stripBom(out);
    return true;
}

// 内置英文目录（由 i18n_en.inc 提供）
const std::unordered_map<std::string, std::string>& builtinEn() {
    static const std::unordered_map<std::string, std::string> m = [] {
        static const char* kEntries[][2] = {
#include "i18n_en.inc"
        };
        std::unordered_map<std::string, std::string> out;
        for (const auto& e : kEntries) out[e[0]] = e[1];
        return out;
    }();
    return m;
}

std::string langDir(const std::string& dataDir) {
    return platform::pathJoin(dataDir, "langs");
}

// 把嵌套对象摊平成点路径（只收集字符串叶子）
void flatten(const json& node, const std::string& prefix,
             std::unordered_map<std::string, std::string>& out) {
    if (node.is_string()) {
        if (!prefix.empty()) out[prefix] = node.get<std::string>();
        return;
    }
    if (!node.is_object()) return;
    for (auto it = node.begin(); it != node.end(); ++it) {
        std::string k = prefix.empty() ? it.key() : prefix + "." + it.key();
        flatten(it.value(), k, out);
    }
}

// 读取 <dataDir>/langs/<code>.cjl 的 strings.gui
bool loadPack(const std::string& dataDir, const std::string& code,
              std::unordered_map<std::string, std::string>& out) {
    std::string text;
    if (!readText(platform::pathJoin(langDir(dataDir), code + ".cjl"), text) || text.empty())
        return false;
    try {
        json j = json::parse(text);
        if (!j.is_object() || !j.contains("strings") || !j["strings"].is_object()) return false;
        const json& strings = j["strings"];
        if (strings.contains("gui")) flatten(strings["gui"], "", out);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<LangInfo> scanLanguages(const std::string& dataDir) {
    std::vector<LangInfo> out;
    out.push_back({"zh", "中文", true});
    out.push_back({"en", "English", true});

    std::vector<LangInfo> ext;
    std::error_code ec;
    fs::path dir(langDir(dataDir));
    if (fs::is_directory(dir, ec)) {
        for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) continue;
            std::string name = it->path().filename().string();
            if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".cjl") != 0) continue;
            std::string code = name.substr(0, name.size() - 4);
            if (!validCode(code)) continue;

            std::string display = code;
            std::string text;
            if (readText(it->path().string(), text)) {
                try {
                    json j = json::parse(text);
                    if (j.is_object() && j.contains("meta") && j["meta"].is_object())
                        display = j["meta"].value("display", code);
                } catch (...) {
                    // 损坏的语言包仍列出，切换时按内置英文/中文兜底
                }
            }
            if (display.empty()) display = code;
            ext.push_back({code, display, false});
        }
    }
    std::sort(ext.begin(), ext.end(),
              [](const LangInfo& a, const LangInfo& b) { return a.code < b.code; });
    for (auto& e : ext) {
        // 外部 en/zh.cjl 只更新显示名，文案仍走内置目录与中文字面量
        if (e.code == "zh") {
            out[0].display = e.display;
            continue;
        }
        if (e.code == "en") {
            out[1].display = e.display;
            continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::string cliCurrentLang(const std::string& dataDir) {
    std::string text;
    if (!readText(platform::pathJoin(dataDir, "config.json"), text)) return "";
    try {
        json j = json::parse(text);
        if (j.is_object() && j.contains("current_lang") && j["current_lang"].is_string())
            return j["current_lang"].get<std::string>();
    } catch (...) {
    }
    return "";
}

} // namespace

void init(const std::string& dataDir, const std::string& configPath,
          const std::string& preferred) {
    {
        std::lock_guard<std::mutex> lk(state().mu);
        state().dataDir = dataDir;
        state().configPath = configPath;
    }
    std::string code = preferred;
    if (code.empty()) code = cliCurrentLang(dataDir);
    if (code.empty()) code = "zh";
    if (!setLanguage(code)) setLanguage("zh");
}

std::vector<LangInfo> languages() {
    std::lock_guard<std::mutex> lk(state().mu);
    return state().langs;
}

bool hasLanguage(const std::string& code) {
    for (const auto& l : languages()) {
        if (l.code == code) return true;
    }
    return false;
}

std::string currentLanguage() {
    std::lock_guard<std::mutex> lk(state().mu);
    return state().lang;
}

std::string currentDisplayName() {
    std::lock_guard<std::mutex> lk(state().mu);
    for (const auto& l : state().langs) {
        if (l.code == state().lang) return l.display;
    }
    return state().lang;
}

std::string languageDir() {
    std::lock_guard<std::mutex> lk(state().mu);
    return langDir(state().dataDir);
}

bool setLanguage(const std::string& code) {
    if (!validCode(code)) return false;

    std::string dataDir;
    {
        std::lock_guard<std::mutex> lk(state().mu);
        dataDir = state().dataDir;
    }

    std::vector<LangInfo> langs = scanLanguages(dataDir);
    bool known = false;
    for (const auto& l : langs) {
        if (l.code == code) {
            known = true;
            break;
        }
    }
    if (!known) return false;

    // 语言包 → 内置英文 → 中文兜底
    std::unordered_map<std::string, std::string> active;
    if (code != "zh") {
        active = builtinEn();
        std::unordered_map<std::string, std::string> pack;
        if (loadPack(dataDir, code, pack)) {
            for (auto& kv : pack) active[kv.first] = kv.second;
        }
    }

    {
        std::lock_guard<std::mutex> lk(state().mu);
        state().lang = code;
        state().langs = std::move(langs);
        state().active.swap(active);
    }
    persistLanguage(code);
    return true;
}

bool persistLanguage(const std::string& code) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(state().mu);
        path = state().configPath;
    }
    if (path.empty()) return false;

    json j = json::object();
    std::string text;
    if (readText(path, text) && !text.empty()) {
        try {
            json parsed = json::parse(text);
            if (parsed.is_object()) j = parsed;
        } catch (...) {
            // gui.json 损坏时重建，不影响启动参数（下次启动回退默认值）
        }
    }
    j["lang"] = code;
    return platform::writeFileAtomic(path, j.dump(2) + "\n");
}

const char* t(const char* key, const char* zh) {
    if (!zh) return "";
    if (!key || !key[0]) return zh;

    std::lock_guard<std::mutex> lk(state().mu);
    State& s = state();
    if (s.lang.empty() || s.lang == "zh" || s.active.empty()) return zh;

    std::string ck = s.lang;
    ck.push_back('\x1f');
    ck.append(key);

    auto it = s.cache.find(ck);
    if (it != s.cache.end()) return it->second.c_str();

    auto a = s.active.find(key);
    if (a == s.active.end()) return zh;

    auto ins = s.cache.emplace(std::move(ck), a->second);
    return ins.first->second.c_str();
}

std::string ts(const char* key, const std::string& zh) {
    return std::string(t(key, zh.c_str()));
}

std::string tf(const char* key, const std::string& zh, std::vector<std::string> args) {
    const std::string fmt = t(key, zh.c_str());
    std::string out;
    out.reserve(fmt.size() + 16);
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{') {
            size_t j = i + 1;
            size_t num = 0;
            bool digits = false;
            while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
                num = num * 10 + (size_t)(fmt[j] - '0');
                digits = true;
                ++j;
            }
            if (digits && j < fmt.size() && fmt[j] == '}') {
                if (num < args.size()) out += args[num];
                i = j;
                continue;
            }
        }
        out += fmt[i];
    }
    return out;
}

} // namespace i18n
} // namespace gui
} // namespace clijudge
