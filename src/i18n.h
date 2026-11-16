// i18n.h
// 显示语言包（与 clijudge CLI 的 displaylang 同一套 .cjl 格式与存放位置）
//
// 语言包位置: <dataDir>/langs/<code>.cjl
//   {
//     "meta": { "name": "ja", "display": "日本語", "version": "1.0.0" },
//     "strings": { "gui": { "nav": { "overview": "概要" }, ... } }
//   }
// CLI 的 strings 节点（help/judge/...）保持不动，GUI 只读取 strings.gui，
// 因此同一个语言包可以同时服务命令行与图形界面。
//
// 文案以中文作为兜底字面量写在调用处:
//   ImGui::Button(i18n::t("common.save", "保存"))
// 当前语言非 zh 时的查找顺序:
//   1. <dataDir>/langs/<code>.cjl 的 strings.gui.<key>
//   2. 内置英文目录（src/i18n_en.inc）
//   3. 调用处的中文字面量
//
// 当前语言持久化到 gui.json 的 "lang" 字段（不影响 CLI 的 config.json）；
// 首次运行没有该字段时沿用 CLI config.json 的 current_lang。

#ifndef CLIJUDGE_GUI_I18N_H
#define CLIJUDGE_GUI_I18N_H

#include <string>
#include <vector>

namespace clijudge {
namespace gui {
namespace i18n {

struct LangInfo {
    std::string code;     // "zh" / "en" / 语言包文件名
    std::string display;  // meta.display，缺省为 code
    bool builtin = false; // 内置语言（zh、en）
};

// 初始化（进程启动时调用一次，需在设置 CLIJUDGE_DATA_DIR 之后）
void init(const std::string& dataDir, const std::string& configPath,
          const std::string& preferred);

// 本地可用语言（内置 zh/en + langs/*.cjl）
std::vector<LangInfo> languages();
bool hasLanguage(const std::string& code);

std::string currentLanguage();
std::string currentDisplayName();

// <dataDir>/langs（语言包目录）
std::string languageDir();

// 切换语言：加载语言包并写入 gui.json；code 不存在时返回 false
bool setLanguage(const std::string& code);
bool persistLanguage(const std::string& code);

// 翻译：key 为点路径，zh 为中文兜底
const char* t(const char* key, const char* zh);
std::string ts(const char* key, const std::string& zh);
// 支持 {0} {1} ... 占位符
std::string tf(const char* key, const std::string& zh, std::vector<std::string> args);

// 在线语言包（i18n_online.cpp，复用 CLI 的语言源设置）
std::vector<std::string> fetchOnlineLanguages();
bool downloadLanguage(const std::string& code);

} // namespace i18n
} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_I18N_H
