// ui_app.cpp
#define SDL_MAIN_HANDLED
#include "ui_app.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <vector>

#include <SDL.h>

#include "imgui.h"
#include "json.hpp"
#include "markdown.h"
#include "data_service.h"
#include "compare_modes.h"

namespace clijudge {
namespace gui {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = (std::string*)data->UserData;
        s->resize((size_t)data->BufTextLen);
        data->Buf = (char*)s->c_str();
    }
    return 0;
}

bool InputString(const char* label, std::string& s, ImGuiInputTextFlags flags = 0,
                 ImVec2 size = ImVec2(0, 0)) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, (char*)s.c_str(), s.size() + 1, flags, resizeCallback, &s);
}

bool InputMultiline(const char* label, std::string& s, ImVec2 size = ImVec2(0, 0),
                    ImGuiInputTextFlags flags = 0) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(label, (char*)s.c_str(), s.size() + 1, size, flags,
                                     resizeCallback, &s);
}

std::string sval(const json& j, const char* key, const std::string& def = "") {
    if (!j.is_object() || !j.contains(key)) return def;
    const json& v = j.at(key);
    if (v.is_string()) return v.get<std::string>();
    return def;
}

int ival(const json& j, const char* key, int def = 0) {
    if (!j.is_object() || !j.contains(key)) return def;
    const json& v = j.at(key);
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_unsigned()) return (int)v.get<unsigned long long>();
    if (v.is_number_float()) return (int)v.get<double>();
    return def;
}

ImU32 statusColor(const std::string& st) {
    if (st == "AC") return IM_COL32(74, 222, 128, 255);
    if (st == "WA" || st == "RE" || st == "CE" || st == "SE") return IM_COL32(248, 113, 113, 255);
    if (st == "TLE" || st == "MLE" || st == "OLE") return IM_COL32(251, 191, 36, 255);
    if (st == "PC") return IM_COL32(250, 204, 21, 255);
    if (st == "SK") return IM_COL32(148, 163, 184, 255);
    if (st.empty() || st == "pending") return IM_COL32(96, 165, 250, 255);
    return IM_COL32(203, 213, 225, 255);
}

std::string fmtMs(int ms) { return std::to_string(ms) + " ms"; }

void openUrl(const std::string& url) {
    if (url.empty()) return;
    SDL_OpenURL(url.c_str());
}

} // namespace

// ─────────────────────────────────────────────────────────
struct UiApp::Impl {
    AppOptions opt;
    DataService svc;
    md::Theme theme;
    float S = 1.0f;

    enum class Task { None, Submit, ContestSubmit, Rejudge };
    Task task = Task::None;
    std::future<JudgeOutcome> fut;
    JudgeOutcome outcome;
    bool hasOutcome = false;

    std::string page = "overview";

    // 缓存
    json problems = json::array();
    json articles = json::array();
    json contests = json::array();
    json submissions = json::array();
    bool dirtyProblems = true, dirtyArticles = true, dirtyContests = true, dirtySubs = true;

    int countProblems = 0, countArticles = 0, countContests = 0, countSubmissions = 0;
    bool dirtyStats = true;
    double lastStats = -10.0;

    std::string search;

    // 题目
    int selProblem = -1;
    json curProblem = nullptr;
    int curProblemId = -1;
    int problemTab = 0;
    bool editingProblem = false;
    struct ProblemEdit {
        std::string title, desc, inputDesc, outputDesc, hint, sampleIn, sampleOut;
        int timeLimit = 1000;
        int memoryLimit = 256;
        // 原样保存 CLI 的 compare_mode，识别不了的值也不会被覆盖成 text_strict
        std::string compareMode = "text_strict";
    } pe;
    json testCases = json::array();
    bool dirtyTestCases = false;

    // 新建题目
    bool reqCreateProblem = false;
    std::string newProblemTitle;

    // 新增测试点
    bool reqAddTc = false;
    std::string tcIn, tcOut;
    int tcScore = 10, tcTime = 0, tcMem = 0;

    // 提交
    std::string code;
    int langIdx = 0;
    std::vector<LangOption> langs;

    // 比赛提交
    int contestProblemIdx = 1;
    json curContestProblem = nullptr;
    int curContestProblemId = -1;
    json contestProblemSubs = json::array();
    bool dirtyContestSubs = false;

    // 文章
    int selArticle = -1;
    json curArticle = nullptr;
    bool reqCreateArticle = false;
    std::string artTitle, artContent;

    // 比赛
    int selContest = -1;
    json curContest = nullptr;
    int contestTab = 0;
    bool reqCreateContest = false;
    std::string cTitle, cStart, cEnd, cProblemIds;
    json board = json::array();
    bool dirtyBoard = true;

    // 提交记录
    int selSubmission = -1;
    json curSubmission = nullptr;
    bool reqSubmissionDetail = false;

    // 删除确认
    int confirmKind = 0;      // 1 题目 2 文章 3 比赛 4 测试点
    int confirmId = 0;
    int confirmExtra = 0;
    bool confirmOpenRequested = false;

    // toast
    std::string toast;
    double toastAt = 0.0;

    Impl(AppOptions o) : opt(std::move(o)), svc(opt.dataDir) {
        S = opt.uiScale <= 0.0f ? 1.0f : opt.uiScale;
        theme.font = opt.font;
        theme.fontSize = std::max(13.0f, 18.0f * S);
        theme.lineSpacing = std::max(2.0f, 4.0f * S);
        langs = svc.languages();
        code = langs.empty() ? std::string() : langs[0].tmpl;
    }

    void notify(const std::string& msg) {
        toast = msg;
        toastAt = ImGui::GetTime();
    }

    void markAllDirty() {
        dirtyProblems = dirtyArticles = dirtyContests = dirtySubs = true;
        dirtyStats = true;
        dirtyTestCases = true;
        dirtyBoard = true;
        dirtyContestSubs = true;
    }

    void reloadProblem() {
        if (curProblemId < 0) return;
        curProblem = svc.problemView(curProblemId);
        testCases = curProblem.is_object() && curProblem.contains("test_cases")
                        ? curProblem["test_cases"]
                        : json::array();
        dirtyTestCases = false;
    }

    void openProblem(int id) {
        selProblem = id;
        curProblemId = id;
        problemTab = 0;
        editingProblem = false;
        hasOutcome = false;
        contestProblemIdx = 1;
        reloadProblem();
    }

    void fillEditBuffers() {
        if (!curProblem.is_object() || !curProblem.contains("problem")) return;
        const json& p = curProblem["problem"];
        pe.title = sval(p, "title");
        pe.desc = sval(p, "description");
        pe.inputDesc = sval(p, "input_desc");
        pe.outputDesc = sval(p, "output_desc");
        pe.hint = sval(p, "hint");
        pe.sampleIn = sval(p, "sample_input");
        pe.sampleOut = sval(p, "sample_output");
        pe.timeLimit = ival(p, "time_limit", 1000);
        pe.memoryLimit = ival(p, "memory_limit", 256);
        pe.compareMode = sval(p, "compare_mode", "text_strict");
    }

    // 轮询异步评测
    void updateAsync() {
        if (task == Task::None || !fut.valid()) return;
        if (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;

        outcome = fut.get();
        task = Task::None;
        hasOutcome = outcome.ok;
        if (outcome.ok) {
            notify("评测完成: " + outcome.status + "  " + std::to_string(outcome.score) + " 分");
        } else {
            notify("评测失败: " + outcome.error);
        }
        dirtySubs = true;
        dirtyStats = true;
        dirtyContestSubs = true;
        dirtyBoard = true;
        if (selSubmission >= 0) curSubmission = svc.submissionGet(selSubmission);
    }

    void startSubmit(bool contest) {
        if (code.empty()) {
            notify("源码为空");
            return;
        }
        if (task != Task::None) {
            notify("已有评测任务进行中");
            return;
        }
        hasOutcome = false;
        if (langs.empty() || langIdx < 0 || langIdx >= (int)langs.size()) {
            notify("没有可用的评测语言");
            return;
        }
        std::string ext = langs[(size_t)langIdx].ext;
        if (contest) {
            if (curContestId() < 0) return;
            task = Task::ContestSubmit;
            fut = svc.submitContestAsync(curContestId(), contestProblemIdx, code, ext, "gui");
        } else {
            if (curProblemId < 0) return;
            task = Task::Submit;
            fut = svc.submitProblemAsync(curProblemId, code, ext, "gui");
        }
    }

    int curContestId() const { return selContest; }

    // ── 通用 UI 组件 ────────────────────────────────────
    void markdownView(const std::string& text) {
        float w = ImGui::GetContentRegionAvail().x;
        if (w < 40.0f * S) w = 40.0f * S;
        md::Render(text, theme, w, openUrl);
    }

    bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(37, 99, 235, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(59, 130, 246, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(29, 78, 216, 255));
        bool r = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return r;
    }

    void statCard(const char* id, const char* label, int value) {
        ImGui::BeginChild(id, ImVec2(150.0f * S, 84.0f * S), true);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(147, 197, 253, 255));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted(std::to_string(value).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::TextUnformatted(label);
        ImGui::EndChild();
    }

    void drawJudgeDetail(const std::string& detailStr) {
        if (detailStr.empty()) return;
        json d;
        try {
            d = json::parse(detailStr);
        } catch (...) {
            ImGui::TextUnformatted("评测详情解析失败");
            return;
        }
        std::string st = sval(d, "status_abbr");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor(st)),
                           "%s  %d / %d 分", st.c_str(), ival(d, "total_score"),
                           ival(d, "max_score"));
        ImGui::Text("用时 %s    内存 %d KB", fmtMs(ival(d, "total_time_ms")).c_str(),
                    ival(d, "max_memory_kb"));
        std::string ce = sval(d, "compile_error");
        if (!ce.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(248, 113, 113, 255));
            ImGui::TextUnformatted("编译错误");
            ImGui::PopStyleColor();
            ImGui::BeginChild("ce", ImVec2(0, 120.0f * S), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(ce.c_str());
            ImGui::EndChild();
        }
        if (!d.contains("subtasks") || !d["subtasks"].is_array()) return;
        int idx = 0;
        for (const auto& stj : d["subtasks"]) {
            ++idx;
            char label[96];
            std::snprintf(label, sizeof(label), "子任务 %d  (%d/%d)", ival(stj, "id", idx),
                          ival(stj, "score"), ival(stj, "max_score"));
            if (!ImGui::CollapsingHeader(label)) continue;
            if (!stj.contains("test_cases") || !stj["test_cases"].is_array()) continue;
            if (ImGui::BeginTable("tc", 5,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("测试点");
                ImGui::TableSetupColumn("状态");
                ImGui::TableSetupColumn("用时");
                ImGui::TableSetupColumn("内存");
                ImGui::TableSetupColumn("得分");
                ImGui::TableHeadersRow();
                for (const auto& tc : stj["test_cases"]) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", ival(tc, "id"));
                    ImGui::TableNextColumn();
                    std::string tst = sval(tc, "status_abbr");
                    if (tst.empty()) tst = sval(tc, "status");
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor(tst)),
                                       "%s", tst.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", fmtMs(ival(tc, "time_ms")).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d KB", ival(tc, "memory_kb"));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", ival(tc, "score"));
                }
                ImGui::EndTable();
            }
        }
    }

    void drawToast() {
        if (toast.empty()) return;
        double age = ImGui::GetTime() - toastAt;
        if (age > 3.2) {
            toast.clear();
            return;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        float alpha = age > 2.6 ? (float)((3.2 - age) / 0.6) : 1.0f;
        ImVec2 ts = ImGui::CalcTextSize(toast.c_str());
        ImVec2 pos(ds.x * 0.5f - ts.x * 0.5f, ds.y - 64.0f * S);
        dl->AddRectFilled(ImVec2(pos.x - 14 * S, pos.y - 8 * S),
                          ImVec2(pos.x + ts.x + 14 * S, pos.y + ts.y + 8 * S),
                          IM_COL32(20, 26, 38, (int)(230 * alpha)), 8.0f);
        dl->AddText(pos, IM_COL32(226, 236, 248, (int)(255 * alpha)), toast.c_str());
    }

    // ── 导航 ────────────────────────────────────────────
    void navButton(const char* label, const char* key) {
        bool active = (page == key);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(37, 99, 235, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(37, 99, 235, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(37, 99, 235, 255));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 37, 50, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(45, 55, 72, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(55, 65, 82, 255));
        }
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(label, ImVec2(w, 36.0f * S))) {
            page = key;
            selProblem = selArticle = selContest = selSubmission = -1;
        }
        ImGui::PopStyleColor(3);
    }

    void drawSidebar() {
        ImGui::BeginChild("sidebar", ImVec2(196.0f * S, 0), true);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(147, 197, 253, 255));
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextUnformatted("CliJudge++");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("本地判题管理");
        ImGui::Separator();
        navButton("概览", "overview");
        navButton("题目", "problems");
        navButton("文章", "articles");
        navButton("比赛", "contests");
        navButton("提交记录", "submissions");
        ImGui::Separator();
        ImGui::TextDisabled("数据目录");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 170.0f * S);
        ImGui::TextUnformatted(opt.dataDir.c_str());
        ImGui::PopTextWrapPos();
        if (!opt.judgeAvailable) {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(251, 191, 36, 255));
            ImGui::TextWrapped("移动端仅支持管理与浏览，本地编译评测不可用。");
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    void drawCompactNav() {
        navButtonSmall("概览", "overview");
        ImGui::SameLine();
        navButtonSmall("题目", "problems");
        ImGui::SameLine();
        navButtonSmall("文章", "articles");
        ImGui::SameLine();
        navButtonSmall("比赛", "contests");
        ImGui::SameLine();
        navButtonSmall("提交", "submissions");
        ImGui::Separator();
    }

    void navButtonSmall(const char* label, const char* key) {
        bool active = (page == key);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(37, 99, 235, 255));
        else ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 37, 50, 255));
        if (ImGui::Button(label)) {
            page = key;
            selProblem = selArticle = selContest = selSubmission = -1;
        }
        ImGui::PopStyleColor();
    }

    // ── 概览 ────────────────────────────────────────────
    void pageOverview() {
        double now = ImGui::GetTime();
        if (dirtyStats || now - lastStats > 5.0) {
            countProblems = svc.countProblems();
            countArticles = svc.countArticles();
            countContests = svc.countContests();
            countSubmissions = svc.countSubmissions();
            dirtyStats = false;
            lastStats = now;
        }

        if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); statCard("c1", "题目", countProblems);
            ImGui::TableNextColumn(); statCard("c2", "文章", countArticles);
            ImGui::TableNextColumn(); statCard("c3", "比赛", countContests);
            ImGui::TableNextColumn(); statCard("c4", "提交", countSubmissions);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::BeginChild("info", ImVec2(0, 0), true)) {
            ImGui::TextUnformatted("数据目录");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "%s", opt.dataDir.c_str());
            ImGui::TextWrapped(
                "与 clijudge 命令行共享同一份数据：与 CLI 放在同一目录时自动使用其 data/ 目录，"
                "修改类操作持有相同的跨进程数据锁。");
            ImGui::Separator();
            if (opt.judgeAvailable) {
                ImGui::TextColored(ImVec4(0.45f, 0.84f, 0.55f, 1.0f), "评测可用");
            } else {
                ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.27f, 1.0f), "评测不可用（移动端）");
            }
            ImGui::TextDisabled("按 F5 或点击下方按钮刷新数据");
            if (ImGui::Button("刷新数据")) {
                markAllDirty();
                notify("已刷新");
            }
        }
        ImGui::EndChild();
    }

    // ── 题目 ────────────────────────────────────────────
    void pageProblems() {
        if (selProblem >= 0) { pageProblemDetail(); return; }

        if (dirtyProblems) {
            problems = svc.problemSummaries();
            dirtyProblems = false;
        }

        ImGui::SetNextItemWidth(220.0f * S);
        InputString("##search", search);
        ImGui::SameLine();
        if (primaryButton("新建题目")) {
            newProblemTitle.clear();
            reqCreateProblem = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("刷新")) dirtyProblems = true;

        ImGui::Spacing();
        if (ImGui::BeginTable("problems", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("标题", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableHeadersRow();
            for (const auto& p : problems) {
                std::string title = sval(p, "title");
                if (!search.empty() && title.find(search) == std::string::npos) continue;
                int id = ival(p, "id");
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", id);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(title.c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("打开")) openProblem(id);
            }
            ImGui::EndTable();
        }
    }

    std::string buildStatement() {
        if (!curProblem.is_object() || !curProblem.contains("problem")) return "";
        const json& p = curProblem["problem"];
        std::string out = "# " + sval(p, "title", "未命名题目") + "\n\n";
        std::string desc = sval(p, "description");
        if (!desc.empty()) out += desc + "\n\n";
        std::string in = sval(p, "input_desc");
        if (!in.empty()) out += "## 输入格式\n\n" + in + "\n\n";
        std::string od = sval(p, "output_desc");
        if (!od.empty()) out += "## 输出格式\n\n" + od + "\n\n";
        std::string si = sval(p, "sample_input");
        if (!si.empty()) out += "## 样例输入\n\n```\n" + si + "\n```\n\n";
        std::string so = sval(p, "sample_output");
        if (!so.empty()) out += "## 样例输出\n\n```\n" + so + "\n```\n\n";
        std::string hint = sval(p, "hint");
        if (!hint.empty()) out += "## 提示\n\n" + hint + "\n\n";
        out += "---\n\n";
        out += "限制: " + std::to_string(ival(p, "time_limit", 1000)) + " ms / " +
               std::to_string(ival(p, "memory_limit", 256)) + " MB  ·  比较模式: " +
               sval(p, "compare_mode", "text_strict");
        return out;
    }

    void pageProblemDetail() {
        if (curProblemId != selProblem || dirtyTestCases) reloadProblem();
        if (!curProblem.is_object()) {
            ImGui::TextUnformatted("题目数据加载失败");
            if (ImGui::Button("返回")) selProblem = -1;
            return;
        }

        if (ImGui::Button("< 返回")) { selProblem = -1; return; }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "#%d  %s", curProblemId,
                           sval(curProblem["problem"], "title").c_str());
        ImGui::SameLine();
        if (ImGui::Button("编辑")) {
            fillEditBuffers();
            editingProblem = true;
            problemTab = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("删除")) {
            confirmKind = 1;
            confirmId = curProblemId;
            confirmOpenRequested = true;
        }

        ImGui::Separator();
        if (ImGui::BeginTabBar("ptabs")) {
            if (ImGui::BeginTabItem("题面")) {
                problemTab = 0;
                if (editingProblem) drawProblemEditor();
                else markdownView(buildStatement());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("测试数据")) {
                problemTab = 1;
                drawTestCasesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("提交评测")) {
                problemTab = 2;
                drawSubmitTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("提交记录")) {
                problemTab = 3;
                drawProblemSubmissions();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void drawProblemEditor() {
        if (InputString("标题", pe.title)) {}
        if (ImGui::BeginTable("meta", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("时间限制 (ms)", &pe.timeLimit);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("内存限制 (MB)", &pe.memoryLimit);
            ImGui::TableNextColumn();
            int cmIdx = compareModeIndex(pe.compareMode);
            std::string cmPreview = cmIdx >= 0
                                        ? std::string(kCompareModes[cmIdx])
                                        : pe.compareMode + "（未识别，保留原值）";
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("比较模式", cmPreview.c_str())) {
                for (int i = 0; i < kCompareModeCount; ++i) {
                    if (ImGui::Selectable(kCompareModes[i], cmIdx == i)) pe.compareMode = kCompareModes[i];
                }
                ImGui::EndCombo();
            }
            ImGui::EndTable();
        }
        ImGui::TextUnformatted("题面 (Markdown)");
        InputMultiline("##desc", pe.desc, ImVec2(-FLT_MIN, 220.0f * S));
        ImGui::TextUnformatted("输入格式");
        InputMultiline("##in", pe.inputDesc, ImVec2(-FLT_MIN, 60.0f * S));
        ImGui::TextUnformatted("输出格式");
        InputMultiline("##out", pe.outputDesc, ImVec2(-FLT_MIN, 60.0f * S));
        ImGui::TextUnformatted("样例输入");
        InputMultiline("##si", pe.sampleIn, ImVec2(-FLT_MIN, 80.0f * S));
        ImGui::TextUnformatted("样例输出");
        InputMultiline("##so", pe.sampleOut, ImVec2(-FLT_MIN, 80.0f * S));
        ImGui::TextUnformatted("提示");
        InputMultiline("##hint", pe.hint, ImVec2(-FLT_MIN, 80.0f * S));

        ImGui::Spacing();
        if (primaryButton("保存")) {
            json fields = {
                {"title", pe.title},
                {"description", pe.desc},
                {"input_desc", pe.inputDesc},
                {"output_desc", pe.outputDesc},
                {"hint", pe.hint},
                {"sample_input", pe.sampleIn},
                {"sample_output", pe.sampleOut},
                {"time_limit", pe.timeLimit},
                {"memory_limit", pe.memoryLimit},
                {"compare_mode", pe.compareMode}
            };
            if (svc.saveProblem(curProblemId, fields)) {
                notify("已保存");
                editingProblem = false;
                dirtyProblems = true;
                dirtyStats = true;
                reloadProblem();
            } else {
                notify("保存失败");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) editingProblem = false;
    }

    void drawTestCasesTab() {
        if (dirtyTestCases) reloadProblem();
        if (primaryButton("新增测试点")) {
            tcIn.clear();
            tcOut.clear();
            tcScore = 10;
            tcTime = 0;
            tcMem = 0;
            reqAddTc = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("刷新")) dirtyTestCases = true;

        ImGui::Spacing();
        if (ImGui::BeginTable("tcs", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50 * S);
            ImGui::TableSetupColumn("分值", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("内存", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("输入", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("输出", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 70 * S);
            ImGui::TableHeadersRow();
            for (const auto& tc : testCases) {
                int id = ival(tc, "id");
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", id);
                ImGui::TableNextColumn();
                ImGui::Text("%d", ival(tc, "score"));
                ImGui::TableNextColumn();
                ImGui::Text("%d ms", ival(tc, "time_limit"));
                ImGui::TableNextColumn();
                ImGui::Text("%d MB", ival(tc, "memory_limit"));
                ImGui::TableNextColumn();
                ImGui::Text("%d 字节", (int)sval(tc, "input_data").size());
                ImGui::TableNextColumn();
                ImGui::Text("%d 字节", (int)sval(tc, "output_data").size());
                ImGui::TableNextColumn();
                ImGui::PushID(id);
                if (ImGui::SmallButton("删除")) {
                    confirmKind = 4;
                    confirmId = curProblemId;
                    confirmExtra = id;
                    confirmOpenRequested = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void drawSubmitTab() {
        if (!opt.judgeAvailable) {
            ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.27f, 1.0f),
                               "Android 端不支持本地编译评测。");
            ImGui::TextWrapped("判题需要调用外部编译器（g++ 等）并运行可执行文件，移动端无法完成。"
                               "请使用桌面版 clijudgepp 或 clijudge 命令行进行评测。");
            return;
        }
        ImGui::SetNextItemWidth(160.0f * S);
        if (ImGui::Combo("语言", &langIdx, [](void* data, int i, const char** out) {
            const auto& list = *(const std::vector<LangOption>*)data;
            *out = list[(size_t)i].name.c_str();
            return true;
        }, &langs, (int)langs.size())) {
        }
        ImGui::SameLine();
        bool hasTmpl = langIdx >= 0 && langIdx < (int)langs.size() && !langs[(size_t)langIdx].tmpl.empty();
        ImGui::BeginDisabled(!hasTmpl);
        if (ImGui::Button("插入模板") && hasTmpl) code = langs[(size_t)langIdx].tmpl;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("清空")) code.clear();

        InputMultiline("##code", code, ImVec2(-FLT_MIN, 260.0f * S),
                       ImGuiInputTextFlags_AllowTabInput);

        if (task == Task::Submit) {
            ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "评测中，请稍候...");
        } else if (primaryButton("提交评测", ImVec2(140.0f * S, 34.0f * S))) {
            startSubmit(false);
        }

        if (hasOutcome && !outcome.detailJson.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("评测结果");
            drawJudgeDetail(outcome.detailJson);
        } else if (!hasOutcome && !outcome.error.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.98f, 0.44f, 0.44f, 1.0f), "%s", outcome.error.c_str());
        }
    }

    void drawProblemSubmissions() {
        if (dirtySubs) {
            submissions = svc.submissionList(1000);
            dirtySubs = false;
        }
        if (ImGui::Button("刷新")) dirtySubs = true;
        ImGui::Spacing();
        if (ImGui::BeginTable("psubs", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 55 * S);
            ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("分数", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("用时", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("内存", ImGuiTableColumnFlags_WidthFixed, 90 * S);
            ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            bool any = false;
            for (const auto& sub : submissions) {
                if (ival(sub, "problem_id") != curProblemId) continue;
                any = true;
                addSubmissionRow(sub, "prow");
            }
            if (!any) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("暂无提交");
            }
            ImGui::EndTable();
        }
    }

    void addSubmissionRow(const json& sub, const char* idPrefix) {
        int id = ival(sub, "id");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID((std::string(idPrefix) + std::to_string(id)).c_str());
        if (ImGui::SmallButton(std::to_string(id).c_str())) {
            selSubmission = id;
            curSubmission = svc.submissionGet(id);
            reqSubmissionDetail = true;
        }
        ImGui::PopID();
        ImGui::TableNextColumn();
        std::string st = sval(sub, "status");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor(st)), "%s", st.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", ival(sub, "score"));
        ImGui::TableNextColumn();
        ImGui::Text("%d ms", ival(sub, "time_used"));
        ImGui::TableNextColumn();
        ImGui::Text("%d KB", ival(sub, "memory_used"));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(sval(sub, "submitted_at").c_str());
    }

    // ── 文章 ────────────────────────────────────────────
    void pageArticles() {
        if (selArticle >= 0) {
            if (!curArticle.is_object()) curArticle = svc.articleView(selArticle);
            if (ImGui::Button("< 返回")) { selArticle = -1; return; }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "%s",
                               sval(curArticle, "title").c_str());
            ImGui::SameLine();
            if (ImGui::Button("删除")) {
                confirmKind = 2;
                confirmId = selArticle;
                confirmOpenRequested = true;
            }
            ImGui::Separator();
            ImGui::BeginChild("articlebody", ImVec2(0, 0), false);
            markdownView(sval(curArticle, "content"));
            ImGui::EndChild();
            return;
        }

        if (dirtyArticles) {
            articles = svc.articleList();
            dirtyArticles = false;
        }
        if (primaryButton("新建文章")) {
            artTitle.clear();
            artContent.clear();
            reqCreateArticle = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("刷新")) dirtyArticles = true;
        ImGui::Spacing();
        if (ImGui::BeginTable("articles", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("标题", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableHeadersRow();
            for (const auto& a : articles) {
                int id = ival(a, "id");
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", id);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sval(a, "title").c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("阅读")) {
                    selArticle = id;
                    curArticle = svc.articleView(id);
                }
            }
            ImGui::EndTable();
        }
    }

    // ── 比赛 ────────────────────────────────────────────
    void pageContests() {
        if (selContest >= 0) { pageContestDetail(); return; }

        if (dirtyContests) {
            contests = svc.contestList();
            dirtyContests = false;
        }
        if (primaryButton("新建比赛")) {
            cTitle.clear();
            cStart.clear();
            cEnd.clear();
            cProblemIds.clear();
            reqCreateContest = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("刷新")) dirtyContests = true;
        ImGui::TextDisabled("开始/结束时间格式: YYYY-MM-DD HH:MM:SS（留空表示不限时）");

        ImGui::Spacing();
        if (ImGui::BeginTable("contests", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 55 * S);
            ImGui::TableSetupColumn("标题", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("开始", ImGuiTableColumnFlags_WidthFixed, 150 * S);
            ImGui::TableSetupColumn("结束", ImGuiTableColumnFlags_WidthFixed, 150 * S);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableHeadersRow();
            for (const auto& ct : contests) {
                int id = ival(ct, "id");
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", id);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sval(ct, "title").c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sval(ct, "start_time").c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sval(ct, "end_time").c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("打开")) {
                    selContest = id;
                    curContest = svc.contestView(id);
                    contestTab = 0;
                    dirtyBoard = true;
                }
            }
            ImGui::EndTable();
        }
    }

    void pageContestDetail() {
        if (!curContest.is_object()) curContest = svc.contestView(selContest);
        if (!curContest.is_object()) {
            ImGui::TextUnformatted("比赛数据加载失败");
            if (ImGui::Button("返回")) selContest = -1;
            return;
        }
        if (ImGui::Button("< 返回")) { selContest = -1; return; }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "#%d  %s", selContest,
                           sval(curContest, "title").c_str());
        ImGui::SameLine();
        if (ImGui::Button("删除")) {
            confirmKind = 3;
            confirmId = selContest;
            confirmOpenRequested = true;
        }
        ImGui::TextDisabled("%s  ~  %s", sval(curContest, "start_time").c_str(),
                            sval(curContest, "end_time").c_str());
        ImGui::Separator();

        if (ImGui::BeginTabBar("ctabs")) {
            if (ImGui::BeginTabItem("概览")) {
                contestTab = 0;
                json ids = curContest.contains("problem_ids") ? curContest["problem_ids"]
                                                              : json::array();
                ImGui::Text("题目数量: %d", (int)ids.size());
                if (ImGui::BeginTable("cprobs", 2,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("序号", ImGuiTableColumnFlags_WidthFixed, 60 * S);
                    ImGui::TableSetupColumn("题目", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (size_t ci = 0; ci < ids.size(); ++ci) {
                        int pid = ids[ci].is_number_integer() ? ids[ci].get<int>() : -1;
                        json pv = svc.problemView(pid);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", (int)ci + 1);
                        ImGui::TableNextColumn();
                        std::string t = pv.is_object() && pv.contains("problem")
                                            ? sval(pv["problem"], "title")
                                            : "（题目不存在）";
                        ImGui::TextUnformatted(t.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("排行榜")) {
                contestTab = 1;
                if (ImGui::Button("刷新排行榜")) dirtyBoard = true;
                if (dirtyBoard) {
                    board = svc.contestLeaderboard(selContest);
                    dirtyBoard = false;
                }
                ImGui::Spacing();
                if (ImGui::BeginTable("board", 4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("排名", ImGuiTableColumnFlags_WidthFixed, 60 * S);
                    ImGui::TableSetupColumn("选手", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("总分", ImGuiTableColumnFlags_WidthFixed, 70 * S);
                    ImGui::TableSetupColumn("通过/提交", ImGuiTableColumnFlags_WidthFixed, 90 * S);
                    ImGui::TableHeadersRow();
                    int rank = 0;
                    if (board.is_array()) {
                        for (const auto& row : board) {
                            ++rank;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", rank);
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(sval(row, "username").c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", ival(row, "score"));
                            ImGui::TableNextColumn();
                            ImGui::Text("%d / %d", ival(row, "accepted"), ival(row, "total"));
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("题目")) {
                contestTab = 2;
                drawContestProblemTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void drawContestProblemTab() {
        json ids = curContest.contains("problem_ids") ? curContest["problem_ids"]
                                                      : json::array();
        if (ids.empty()) {
            ImGui::TextDisabled("比赛没有题目");
            return;
        }
        if (contestProblemIdx < 1) contestProblemIdx = 1;
        if (contestProblemIdx > (int)ids.size()) contestProblemIdx = (int)ids.size();

        char preview[96];
        int pid = ids[contestProblemIdx - 1].is_number_integer()
                      ? ids[contestProblemIdx - 1].get<int>() : -1;
        json pv = svc.problemView(pid);
        std::string ptitle = pv.is_object() && pv.contains("problem")
                                 ? sval(pv["problem"], "title") : "（题目不存在）";
        std::snprintf(preview, sizeof(preview), "%d. %s", contestProblemIdx, ptitle.c_str());

        ImGui::SetNextItemWidth(300.0f * S);
        if (ImGui::BeginCombo("题目", preview)) {
            for (size_t ci = 0; ci < ids.size(); ++ci) {
                int id = ids[ci].is_number_integer() ? ids[ci].get<int>() : -1;
                json t = svc.problemView(id);
                std::string title = t.is_object() && t.contains("problem")
                                        ? sval(t["problem"], "title") : "（题目不存在）";
                char label[128];
                std::snprintf(label, sizeof(label), "%d. %s", (int)ci + 1, title.c_str());
                if (ImGui::Selectable(label, contestProblemIdx == (int)ci + 1)) {
                    contestProblemIdx = (int)ci + 1;
                    dirtyContestSubs = true;
                    hasOutcome = false;
                }
            }
            ImGui::EndCombo();
        }

        if (!pv.is_object()) return;
        const json& p = pv["problem"];
        std::string statement = "# " + ptitle + "\n\n" + sval(p, "description");
        std::string si = sval(p, "sample_input");
        std::string so = sval(p, "sample_output");
        if (!si.empty()) statement += "\n\n## 样例输入\n\n```\n" + si + "\n```";
        if (!so.empty()) statement += "\n\n## 样例输出\n\n```\n" + so + "\n```";
        markdownView(statement);

        ImGui::Separator();
        if (dirtyContestSubs) {
            contestProblemSubs = svc.contestProblemSubmissions(selContest, contestProblemIdx);
            dirtyContestSubs = false;
        }
        ImGui::TextUnformatted("该题提交");
        if (ImGui::BeginTable("csub", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                              ImVec2(0, 130.0f * S))) {
            ImGui::TableSetupColumn("提交者", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("结果", ImGuiTableColumnFlags_WidthFixed, 70 * S);
            ImGui::TableSetupColumn("分数", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("用时", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 150 * S);
            ImGui::TableHeadersRow();
            if (contestProblemSubs.is_array()) {
                for (const auto& sub : contestProblemSubs) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(sval(sub, "username").c_str());
                    ImGui::TableNextColumn();
                    std::string st = sval(sub, "result");
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor(st)),
                                       "%s", st.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", ival(sub, "score"));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d ms", ival(sub, "time_used"));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(sval(sub, "submitted_at").c_str());
                }
            }
            ImGui::EndTable();
        }

        if (!opt.judgeAvailable) {
            ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.27f, 1.0f),
                               "移动端不支持比赛本地评测。");
            return;
        }
        ImGui::Spacing();
        ImGui::SetNextItemWidth(160.0f * S);
        const char* curLangName = (langIdx >= 0 && langIdx < (int)langs.size())
                                      ? langs[(size_t)langIdx].name.c_str()
                                      : "";
        if (ImGui::BeginCombo("语言", curLangName)) {
            for (int li = 0; li < (int)langs.size(); ++li) {
                if (ImGui::Selectable(langs[(size_t)li].name.c_str(), langIdx == li)) {
                    langIdx = li;
                    code = langs[(size_t)li].tmpl;
                }
            }
            ImGui::EndCombo();
        }
        InputMultiline("##ccode", code, ImVec2(-FLT_MIN, 160.0f * S),
                       ImGuiInputTextFlags_AllowTabInput);
        if (task == Task::ContestSubmit) {
            ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "评测中，请稍候...");
        } else if (primaryButton("提交比赛题目")) {
            startSubmit(true);
        }
        if (hasOutcome && !outcome.detailJson.empty()) {
            ImGui::Separator();
            drawJudgeDetail(outcome.detailJson);
        }
    }

    // ── 提交记录 ────────────────────────────────────────
    void pageSubmissions() {
        if (dirtySubs) {
            submissions = svc.submissionList(1000);
            dirtySubs = false;
        }
        if (ImGui::Button("刷新")) dirtySubs = true;
        ImGui::SameLine();
        ImGui::TextDisabled("共 %d 条（最多显示 1000 条）", (int)submissions.size());
        ImGui::Spacing();
        if (ImGui::BeginTable("subs", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 55 * S);
            ImGui::TableSetupColumn("题目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("分数", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("用时", ImGuiTableColumnFlags_WidthFixed, 80 * S);
            ImGui::TableSetupColumn("内存", ImGuiTableColumnFlags_WidthFixed, 90 * S);
            ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 150 * S);
            ImGui::TableHeadersRow();
            for (const auto& sub : submissions) {
                int id = ival(sub, "id");
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(id);
                if (ImGui::SmallButton(std::to_string(id).c_str())) {
                    selSubmission = id;
                    curSubmission = svc.submissionGet(id);
                    reqSubmissionDetail = true;
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sval(sub, "problem_title").c_str());
                ImGui::TableNextColumn();
                std::string st = sval(sub, "status");
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor(st)),
                                   "%s", st.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", ival(sub, "score"));
                ImGui::TableNextColumn();
                ImGui::Text("%d ms", ival(sub, "time_used"));
                ImGui::TableNextColumn();
                ImGui::Text("%d KB", ival(sub, "memory_used"));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sval(sub, "submitted_at").c_str());
            }
            ImGui::EndTable();
        }
    }

    // ── 弹窗 ────────────────────────────────────────────
    void popups() {
        if (reqCreateProblem) {
            ImGui::OpenPopup("新建题目");
            reqCreateProblem = false;
        }
        if (ImGui::BeginPopupModal("新建题目", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(360.0f * S);
            InputString("标题", newProblemTitle);
            if (primaryButton("创建")) {
                if (newProblemTitle.empty()) {
                    notify("标题不能为空");
                } else {
                    int id = svc.createProblem(newProblemTitle);
                    if (id > 0) {
                        notify("已创建题目 #" + std::to_string(id));
                        dirtyProblems = true;
                        dirtyStats = true;
                        ImGui::CloseCurrentPopup();
                    } else {
                        notify("创建失败");
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (reqCreateArticle) {
            ImGui::OpenPopup("新建文章");
            reqCreateArticle = false;
        }
        if (ImGui::BeginPopupModal("新建文章", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(420.0f * S);
            InputString("标题", artTitle);
            ImGui::SetNextItemWidth(520.0f * S);
            InputMultiline("Markdown 正文", artContent, ImVec2(520.0f * S, 260.0f * S));
            if (primaryButton("创建")) {
                if (artTitle.empty()) notify("标题不能为空");
                else {
                    int id = svc.createArticle(artTitle, artContent);
                    notify(id > 0 ? "已创建文章 #" + std::to_string(id) : "创建失败");
                    dirtyArticles = true;
                    dirtyStats = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (reqCreateContest) {
            ImGui::OpenPopup("新建比赛");
            reqCreateContest = false;
        }
        if (ImGui::BeginPopupModal("新建比赛", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(420.0f * S);
            InputString("标题", cTitle);
            ImGui::SetNextItemWidth(420.0f * S);
            InputString("开始时间", cStart);
            ImGui::SetNextItemWidth(420.0f * S);
            InputString("结束时间", cEnd);
            ImGui::SetNextItemWidth(420.0f * S);
            InputString("题目 ID（逗号分隔）", cProblemIds);
            if (primaryButton("创建")) {
                if (cTitle.empty()) {
                    notify("标题不能为空");
                } else {
                    std::vector<int> ids;
                    std::string cur;
                    // 中文逗号（UTF-8 三字节）统一按分隔符处理
                    std::string raw = cProblemIds;
                    for (size_t p = raw.find("，"); p != std::string::npos; p = raw.find("，", p))
                        raw.replace(p, 3, ",");
                    for (char ch : raw) {
                        if (ch == ',' || ch == ' ' || ch == '\n' || ch == '\t') {
                            if (!cur.empty()) { ids.push_back(std::atoi(cur.c_str())); cur.clear(); }
                        } else {
                            cur.push_back(ch);
                        }
                    }
                    if (!cur.empty()) ids.push_back(std::atoi(cur.c_str()));
                    int id = svc.createContest(cTitle, cStart, cEnd, ids);
                    notify(id > 0 ? "已创建比赛 #" + std::to_string(id) : "创建失败");
                    dirtyContests = true;
                    dirtyStats = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (reqAddTc) {
            ImGui::OpenPopup("新增测试点");
            reqAddTc = false;
        }
        if (ImGui::BeginPopupModal("新增测试点", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("输入数据");
            InputMultiline("##tcin", tcIn, ImVec2(520.0f * S, 140.0f * S));
            ImGui::TextUnformatted("输出数据");
            InputMultiline("##tcout", tcOut, ImVec2(520.0f * S, 140.0f * S));
            ImGui::SetNextItemWidth(120.0f * S);
            ImGui::InputInt("分值", &tcScore);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f * S);
            ImGui::InputInt("时间(0=默认)", &tcTime);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f * S);
            ImGui::InputInt("内存(0=默认)", &tcMem);
            if (primaryButton("添加")) {
                int id = svc.addTestCase(curProblemId, tcIn, tcOut, tcScore, tcTime, tcMem);
                notify(id > 0 ? "已添加测试点 #" + std::to_string(id) : "添加失败");
                dirtyTestCases = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (reqSubmissionDetail) {
            ImGui::OpenPopup("提交详情");
            reqSubmissionDetail = false;
        }
        if (ImGui::BeginPopupModal("提交详情", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (curSubmission.is_object()) {
                ImGui::Text("#%d  %s", ival(curSubmission, "id"),
                            sval(curSubmission, "problem_title").c_str());
                std::string st = sval(curSubmission, "status");
                ImGui::Text("状态: ");
                ImGui::SameLine();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusColor(st)), "%s",
                                   st.c_str());
                ImGui::Text("分数 %d  用时 %d ms  内存 %d KB  重判 %d 次",
                            ival(curSubmission, "score"), ival(curSubmission, "time_used"),
                            ival(curSubmission, "memory_used"),
                            ival(curSubmission, "judge_times", 1));
                ImGui::Text("提交时间: %s", sval(curSubmission, "submitted_at").c_str());
                ImGui::Text("源码: %s", sval(curSubmission, "file_path").c_str());
                ImGui::Separator();
                ImGui::BeginChild("detail", ImVec2(620.0f * S, 340.0f * S), true);
                if (curSubmission.contains("judge_detail")) {
                    drawJudgeDetail(curSubmission["judge_detail"].dump());
                } else {
                    ImGui::TextDisabled("暂无评测详情");
                }
                ImGui::EndChild();
                if (opt.judgeAvailable) {
                    if (task == Task::Rejudge) {
                        ImGui::TextColored(ImVec4(0.58f, 0.77f, 0.99f, 1.0f), "重判中...");
                    } else if (primaryButton("重判")) {
                        task = Task::Rejudge;
                        fut = svc.rejudgeAsync(ival(curSubmission, "id"));
                    }
                    ImGui::SameLine();
                }
            } else {
                ImGui::TextUnformatted("记录不存在");
            }
            if (ImGui::Button("关闭")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (confirmOpenRequested) {
            ImGui::OpenPopup("确认删除");
            confirmOpenRequested = false;
        }
        if (ImGui::BeginPopupModal("确认删除", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* what = "该项";
            if (confirmKind == 1) what = "题目（含全部测试点）";
            else if (confirmKind == 2) what = "文章";
            else if (confirmKind == 3) what = "比赛";
            else if (confirmKind == 4) what = "测试点";
            ImGui::Text("确认删除%s？此操作不可撤销。", what);
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 38, 38, 255));
            if (ImGui::Button("删除")) {
                if (confirmKind == 1) {
                    svc.deleteProblem(confirmId);
                    selProblem = -1;
                } else if (confirmKind == 2) {
                    svc.deleteArticle(confirmId);
                    selArticle = -1;
                } else if (confirmKind == 3) {
                    svc.deleteContest(confirmId);
                    selContest = -1;
                } else if (confirmKind == 4) {
                    svc.deleteTestCase(confirmId, confirmExtra);
                    dirtyTestCases = true;
                }
                ImGui::PopStyleColor();
                confirmKind = 0;
                markAllDirty();
                notify("已删除");
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消")) {
                confirmKind = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ── 主循环 ──────────────────────────────────────────
    void Frame() {
        updateAsync();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * S, 10 * S));
        bool narrow = vp->WorkSize.x < 760.0f * S;
        float contentW = vp->WorkSize.x;
        if (!narrow) {
            drawSidebar();
            ImGui::SameLine();
            contentW -= 200.0f * S;
        }
        ImGui::BeginChild("content", ImVec2(0, 0), false);
        if (narrow) drawCompactNav();

        if (page == "overview") pageOverview();
        else if (page == "problems") pageProblems();
        else if (page == "articles") pageArticles();
        else if (page == "contests") pageContests();
        else if (page == "submissions") pageSubmissions();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        popups();
        ImGui::End();
        drawToast();
        (void)contentW;
    }
};

UiApp::UiApp(AppOptions opts) : impl_(new Impl(std::move(opts))) {}
UiApp::~UiApp() { delete impl_; }
void UiApp::Frame() { impl_->Frame(); }

} // namespace gui
} // namespace clijudge
