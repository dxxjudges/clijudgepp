// gui_main.cpp
// CliJudge++ GUI 入口：本地 HTTP 服务 + 复用 clijudge 数据层/评测层
//
// 说明:
//   · 不另起一套数据格式：题目/文章/比赛/提交全部走 ../main/include 中的
//     ProblemStore / ArticleStore / ContestStore / SubmitStore
//   · 数据目录与 CLI 共用（gui.json 可显式指定，默认自动探测同目录 main/data）
//   · 修改类请求持有与 CLI 相同的跨进程数据锁 (.clijudge.lock)
//   · 前端资源在构建期内嵌，单文件即可运行

// winsock2.h 必须先于 windows.h（platform.h/judge.h 会引入 windows.h）
#include "http_server.h"

#include "json.hpp"
#include "platform.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "miniz_impl.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "article.h"
#include "contest.h"
#include "problem.h"
#include "submit.h"
#include "settings.h"
#include "lang.h"

#include "gui_config.h"
#include "web_assets.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <filesystem>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const char* kVersion = "1.0.0";

volatile bool g_running = true;
clijudge::gui::GuiConfig g_cfg;
std::string g_dataDir;

// ── 通用工具 ───────────────────────────────────────────────
std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int toInt(const std::string& s, int def = 0) {
    try { return std::stoi(s); } catch (...) { return def; }
}

std::string lowerExt(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

clijudge::gui::HttpResponse jsonRes(const json& j, int status = 200) {
    clijudge::gui::HttpResponse res;
    res.status = status;
    res.contentType = "application/json; charset=utf-8";
    res.body = j.dump();
    return res;
}

clijudge::gui::HttpResponse okRes(const json& extra = json::object()) {
    json j = json::object();
    j["ok"] = true;
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    return jsonRes(j);
}

clijudge::gui::HttpResponse errRes(int status, const std::string& msg) {
    return jsonRes(json{{"ok", false}, {"error", msg}}, status);
}

// 修改类请求持有跨进程数据锁（与 CLI 一致，防止并发写坏索引）
class ScopedDataLock {
public:
    ScopedDataLock() {
        ok_ = lock_.lock(clijudge::platform::pathJoin(g_dataDir, ".clijudge.lock"));
    }
    bool ok() const { return ok_; }
private:
    clijudge::platform::DataLock lock_;
    bool ok_ = false;
};

// ── 题面 / 测试数据字段 ─────────────────────────────────────
const std::vector<std::string>& problemFieldKeys() {
    static const std::vector<std::string> k = {
        "title", "description", "input_desc", "output_desc", "hint",
        "sample_input", "sample_output", "time_limit", "memory_limit",
        "is_public", "is_hidden", "subtask_mode", "problem_type",
        "compare_mode", "float_abs_tolerance", "float_rel_tolerance",
        "spj_code", "special_judge_exe", "answer_file_extension",
        "source_file_name", "allowed_languages", "subtask_dependence",
        "interactor_code", "interactor_data", "interactor_name",
        "grader_files", "generator_code", "generator_exe"
    };
    return k;
}

json pickProblemFields(const json& body) {
    json out = json::object();
    for (const auto& k : problemFieldKeys()) {
        auto it = body.find(k);
        if (it != body.end()) out[k] = it.value();
    }
    // 标题不能为空，避免列表里出现无标题题目
    auto t = out.find("title");
    if (t != out.end() && (!t->is_string() || t->get<std::string>().empty())) {
        out.erase("title");
    }
    return out;
}

clijudge::problem::TestCase testCaseFromJson(const json& j) {
    clijudge::problem::TestCase tc{};
    tc.id = clijudge::settings::jsonValue<int>(j, "id", 0);
    tc.inputData = clijudge::settings::jsonValue<std::string>(j, "input_data", "");
    tc.outputData = clijudge::settings::jsonValue<std::string>(j, "output_data", "");
    tc.inputFile = clijudge::settings::jsonValue<std::string>(j, "input_file", "");
    tc.outputFile = clijudge::settings::jsonValue<std::string>(j, "output_file", "");
    tc.score = clijudge::settings::jsonValue<int>(j, "score", 25);
    tc.timeLimit = clijudge::settings::jsonValue<int>(j, "time_limit", -1);
    tc.memoryLimit = clijudge::settings::jsonValue<int>(j, "memory_limit", -1);
    tc.sortOrder = clijudge::settings::jsonValue<int>(j, "sort_order", tc.id);
    tc.subtaskId = clijudge::settings::jsonValue<int>(j, "subtask_id", 1);
    return tc;
}

// ── 提交源码落盘（GUI 直接粘贴代码时保留源文件，便于 rejudge） ─────
std::string saveSourceCode(const std::string& code, const std::string& ext) {
    static std::atomic<uint64_t> seq{0};
    std::string dir = clijudge::platform::pathJoin(g_dataDir, "gui_src");
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string safeExt = ".cpp";
    std::string e = lowerExt(ext);
    if (!e.empty() && e.size() <= 12 && e[0] == '.') {
        bool valid = true;
        for (size_t i = 1; i < e.size(); ++i) {
            if (!std::isalnum((unsigned char)e[i]) && e[i] != '_' && e[i] != '-') {
                valid = false;
                break;
            }
        }
        if (valid) safeExt = e;
    }

    std::string name = "sub_" + std::to_string(clijudge::platform::pid()) + "_"
                     + std::to_string(clijudge::platform::tickMs()) + "_"
                     + std::to_string(seq.fetch_add(1)) + safeExt;
    std::string path = clijudge::platform::pathJoin(dir, name);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return "";
    f.write(code.data(), (std::streamsize)code.size());
    f.flush();
    return f.good() ? path : "";
}

// 请求体可携带 file_path（服务器本地文件）或 code + ext（粘贴源码）
std::string resolveSubmissionSource(const json& body) {
    std::string filePath = clijudge::settings::jsonValue<std::string>(body, "file_path", "");
    std::string code = clijudge::settings::jsonValue<std::string>(body, "code", "");
    if (code.empty()) return filePath;
    std::string ext = clijudge::settings::jsonValue<std::string>(body, "ext", ".cpp");
    return saveSourceCode(code, ext);
}

json judgeDetailOf(const std::string& raw) {
    if (raw.empty()) return nullptr;
    try { return json::parse(raw); } catch (...) { return nullptr; }
}

// ── 静态资源 ───────────────────────────────────────────────
clijudge::gui::HttpResponse serveAsset(const clijudge::gui::HttpRequest& req) {
    std::string name = req.path == "/" ? "index.html"
                      : clijudge::gui::urlDecode(req.path.substr(1));
    if (name == "favicon.ico") {
        clijudge::gui::HttpResponse res;
        res.status = 204;
        res.body.clear();
        return res;
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return errRes(404, "not found");
    }
    auto& files = clijudge::gui::assets::files();
    auto it = files.find(name);
    if (it == files.end()) return errRes(404, "not found");

    clijudge::gui::HttpResponse res;
    res.status = 200;
    res.contentType = it->second.mime;
    res.body = it->second.data;
    return res;
}

// ── API 分发 ───────────────────────────────────────────────
clijudge::gui::HttpResponse handleApi(const clijudge::gui::HttpRequest& req) {
    const std::vector<std::string> segs = splitPath(req.path);
    const std::string& method = req.method;

    if (segs.size() < 2 || segs[0] != "api") return errRes(404, "unknown api");

    // 状态总览
    if (segs.size() == 2 && segs[1] == "status" && method == "GET") {
        clijudge::problem::ProblemStore ps(g_dataDir);
        clijudge::article::ArticleStore as(g_dataDir);
        clijudge::contest::ContestStore cs(g_dataDir);
        clijudge::submit::SubmitStore ss(g_dataDir);
        json j = {
            {"ok", true},
            {"version", kVersion},
            {"data_dir", g_dataDir},
            {"lang_configured", clijudge::lang::isLangConfigured()},
            {"counts", {
                {"problems", ps.count()},
                {"articles", as.count()},
                {"contests", (int)cs.list().size()},
                {"submissions", ss.count()}
            }}
        };
        return jsonRes(j);
    }

    // ── 题目 ────────────────────────────────────────────────
    if (segs.size() >= 2 && segs[1] == "problems") {
        // /api/problems
        if (segs.size() == 2) {
            if (method == "GET") {
                int l = toInt(req.query.count("l") ? req.query.at("l") : "1", 1);
                int r = toInt(req.query.count("r") ? req.query.at("r") : "1000", 1000);
                if (l < 1) l = 1;
                if (r < l) r = l;
                clijudge::problem::ProblemStore store(g_dataDir);
                return jsonRes({
                    {"ok", true}, {"total", store.count()},
                    {"items", store.list(l, r)}
                });
            }
            if (method == "POST") {
                json body;
                try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                catch (...) { return errRes(400, "invalid json body"); }
                std::string title = clijudge::settings::jsonValue<std::string>(body, "title", "");
                if (title.empty()) return errRes(400, "title is required");

                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::problem::ProblemStore store(g_dataDir);
                int id = store.create(title);
                json updates = pickProblemFields(body);
                updates.erase("title");
                if (!updates.empty()) store.edit(id, updates);
                return jsonRes({{"ok", true}, {"id", id}}, 201);
            }
            return errRes(405, "method not allowed");
        }

        int pid = toInt(segs[2], -1);
        if (pid <= 0) return errRes(400, "invalid problem id");

        // /api/problems/{id}
        if (segs.size() == 3) {
            if (method == "GET") {
                clijudge::problem::ProblemStore store(g_dataDir);
                json p = store.view(pid);
                if (p.is_null()) return errRes(404, "problem not found");
                return jsonRes(p);
            }
            if (method == "PUT" || method == "PATCH") {
                json body;
                try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                catch (...) { return errRes(400, "invalid json body"); }
                json updates = pickProblemFields(body);
                if (updates.empty()) return errRes(400, "no updatable fields");
                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::problem::ProblemStore store(g_dataDir);
                if (!store.edit(pid, updates)) return errRes(404, "problem not found");
                return okRes({{"id", pid}});
            }
            if (method == "DELETE") {
                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::problem::ProblemStore store(g_dataDir);
                if (!store.deleteProblem(pid)) return errRes(404, "problem not found");
                return okRes({{"id", pid}});
            }
            return errRes(405, "method not allowed");
        }

        // /api/problems/{id}/testcases[/{tcId}]
        if (segs[3] == "testcases") {
            if (segs.size() == 4) {
                if (method == "GET") {
                    clijudge::problem::ProblemStore store(g_dataDir);
                    if (store.view(pid).is_null()) return errRes(404, "problem not found");
                    return jsonRes({{"ok", true}, {"items", store.getTestCases(pid)}});
                }
                if (method == "POST") {
                    json body;
                    try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                    catch (...) { return errRes(400, "invalid json body"); }
                    ScopedDataLock lk;
                    if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                    clijudge::problem::ProblemStore store(g_dataDir);
                    if (store.view(pid).is_null()) return errRes(404, "problem not found");

                    json testCases = store.getTestCases(pid);
                    int nextId = 1;
                    for (const auto& tc : testCases) {
                        int id = clijudge::settings::jsonValue<int>(tc, "id", 0);
                        if (id >= nextId) nextId = id + 1;
                    }
                    clijudge::problem::TestCase tc{};
                    tc.id = nextId;
                    tc.inputData = clijudge::settings::jsonValue<std::string>(body, "input_data", "");
                    tc.outputData = clijudge::settings::jsonValue<std::string>(body, "output_data", "");
                    tc.score = clijudge::settings::jsonValue<int>(body, "score", 25);
                    tc.timeLimit = clijudge::settings::jsonValue<int>(body, "time_limit", -1);
                    tc.memoryLimit = clijudge::settings::jsonValue<int>(body, "memory_limit", -1);
                    tc.sortOrder = nextId;
                    tc.subtaskId = clijudge::settings::jsonValue<int>(body, "subtask_id", 1);
                    if (!store.addTestCase(pid, tc)) return errRes(500, "failed to add test case");
                    return jsonRes({{"ok", true}, {"id", nextId}}, 201);
                }
                return errRes(405, "method not allowed");
            }

            if (segs.size() == 5) {
                int tcId = toInt(segs[4], -1);
                if (tcId <= 0) return errRes(400, "invalid test case id");
                if (method == "DELETE") {
                    ScopedDataLock lk;
                    if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                    clijudge::problem::ProblemStore store(g_dataDir);
                    if (!store.deleteTestCase(pid, tcId)) return errRes(404, "test case not found");
                    return okRes({{"id", tcId}});
                }
                if (method == "PUT" || method == "PATCH") {
                    json body;
                    try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                    catch (...) { return errRes(400, "invalid json body"); }
                    ScopedDataLock lk;
                    if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                    clijudge::problem::ProblemStore store(g_dataDir);
                    json testCases = store.getTestCases(pid);
                    json merged = nullptr;
                    for (const auto& tc : testCases) {
                        if (clijudge::settings::jsonValue<int>(tc, "id", 0) == tcId) { merged = tc; break; }
                    }
                    if (merged.is_null()) return errRes(404, "test case not found");
                    for (const char* k : {"input_data", "output_data", "score", "time_limit",
                                          "memory_limit", "subtask_id", "sort_order", "input_file", "output_file"}) {
                        auto it = body.find(k);
                        if (it != body.end()) merged[k] = it.value();
                    }
                    // 复用 delete + add 保留原 id（ProblemStore 未提供单点更新）
                    store.deleteTestCase(pid, tcId);
                    if (!store.addTestCase(pid, testCaseFromJson(merged))) {
                        return errRes(500, "failed to update test case");
                    }
                    return okRes({{"id", tcId}});
                }
                return errRes(405, "method not allowed");
            }
            return errRes(404, "unknown testcase route");
        }

        // /api/problems/{id}/submit
        if (segs.size() == 4 && segs[3] == "submit" && method == "POST") {
            json body;
            try { body = json::parse(req.body.empty() ? "{}" : req.body); }
            catch (...) { return errRes(400, "invalid json body"); }

            clijudge::problem::ProblemStore store(g_dataDir);
            json p = store.view(pid);
            if (p.is_null()) return errRes(404, "problem not found");

            std::string filePath = resolveSubmissionSource(body);
            if (filePath.empty()) return errRes(400, "code or file_path is required");
            if (!fs::exists(filePath)) return errRes(400, "source file not found: " + filePath);

            // 评测（与 CLI 相同链路：沙箱编译 + 运行 + 比对）
            auto sub = store.submit(pid, filePath);
            std::string username = clijudge::settings::jsonValue<std::string>(body, "username", "");
            std::string title = p.contains("problem")
                ? clijudge::settings::jsonValue<std::string>(p["problem"], "title", "") : "";
            int sid = clijudge::submit::addSubmission(g_dataDir, pid, title, filePath,
                             sub.status, sub.score, sub.timeUsed, sub.memoryUsed,
                             username, sub.judgeDetail);

            json j = {
                {"ok", true}, {"submission_id", sid}, {"problem_id", pid},
                {"status", sub.status}, {"score", sub.score},
                {"time_used", sub.timeUsed}, {"memory_used", sub.memoryUsed},
                {"file_path", filePath}
            };
            json detail = judgeDetailOf(sub.judgeDetail);
            if (!detail.is_null()) j["judge_detail"] = detail;
            return jsonRes(j);
        }

        return errRes(404, "unknown problem route");
    }

    // ── 文章 ────────────────────────────────────────────────
    if (segs.size() >= 2 && segs[1] == "articles") {
        if (segs.size() == 2) {
            if (method == "GET") {
                int l = toInt(req.query.count("l") ? req.query.at("l") : "1", 1);
                int r = toInt(req.query.count("r") ? req.query.at("r") : "1000", 1000);
                clijudge::article::ArticleStore store(g_dataDir);
                json items = store.list(l < 1 ? 1 : l, r);
                json out = json::array();
                for (const auto& a : items) {
                    out.push_back({
                        {"id", clijudge::settings::jsonValue<int>(a, "id", 0)},
                        {"title", clijudge::settings::jsonValue<std::string>(a, "title", "")},
                        {"created_at", clijudge::settings::jsonValue<std::string>(a, "created_at", "")},
                        {"updated_at", clijudge::settings::jsonValue<std::string>(a, "updated_at", "")},
                        {"size", clijudge::settings::jsonValue<std::string>(a, "content", "").size()}
                    });
                }
                return jsonRes({{"ok", true}, {"total", store.count()}, {"items", out}});
            }
            if (method == "POST") {
                json body;
                try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                catch (...) { return errRes(400, "invalid json body"); }
                std::string title = clijudge::settings::jsonValue<std::string>(body, "title", "");
                if (title.empty()) return errRes(400, "title is required");
                std::string content = clijudge::settings::jsonValue<std::string>(body, "content", "");
                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::article::ArticleStore store(g_dataDir);
                int id = store.create(title, "", content);
                return jsonRes({{"ok", true}, {"id", id}}, 201);
            }
            return errRes(405, "method not allowed");
        }

        int aid = toInt(segs[2], -1);
        if (aid <= 0) return errRes(400, "invalid article id");
        if (segs.size() == 3) {
            if (method == "GET") {
                clijudge::article::ArticleStore store(g_dataDir);
                json a = store.view(aid);
                if (a.is_null()) return errRes(404, "article not found");
                return jsonRes(a);
            }
            if (method == "DELETE") {
                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::article::ArticleStore store(g_dataDir);
                if (!store.deleteArticle(aid)) return errRes(404, "article not found");
                return okRes({{"id", aid}});
            }
            return errRes(405, "method not allowed");
        }
        return errRes(404, "unknown article route");
    }

    // ── 比赛 ────────────────────────────────────────────────
    if (segs.size() >= 2 && segs[1] == "contests") {
        if (segs.size() == 2) {
            if (method == "GET") {
                clijudge::contest::ContestStore store(g_dataDir);
                json items = store.list();
                json out = json::array();
                for (const auto& c : items) {
                    int n = 0;
                    auto pids = c.find("problem_ids");
                    if (pids != c.end() && pids->is_array()) n = (int)pids->size();
                    out.push_back({
                        {"id", clijudge::settings::jsonValue<int>(c, "id", 0)},
                        {"title", clijudge::settings::jsonValue<std::string>(c, "title", "")},
                        {"start_time", clijudge::settings::jsonValue<std::string>(c, "start_time", "")},
                        {"end_time", clijudge::settings::jsonValue<std::string>(c, "end_time", "")},
                        {"problem_count", n}
                    });
                }
                return jsonRes({{"ok", true}, {"total", (int)out.size()}, {"items", out}});
            }
            if (method == "POST") {
                json body;
                try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                catch (...) { return errRes(400, "invalid json body"); }
                std::string title = clijudge::settings::jsonValue<std::string>(body, "title", "");
                if (title.empty()) return errRes(400, "title is required");
                std::string startTime = clijudge::settings::jsonValue<std::string>(body, "start_time", "");
                std::string endTime = clijudge::settings::jsonValue<std::string>(body, "end_time", "");
                std::vector<int> pids;
                if (body.contains("problem_ids") && body["problem_ids"].is_array()) {
                    for (const auto& v : body["problem_ids"]) {
                        if (v.is_number_integer()) pids.push_back(v.get<int>());
                        else if (v.is_string()) pids.push_back(toInt(v.get<std::string>(), 0));
                    }
                }
                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::contest::ContestStore store(g_dataDir);
                int id = store.create(title, startTime, endTime, pids);
                return jsonRes({{"ok", true}, {"id", id}}, 201);
            }
            return errRes(405, "method not allowed");
        }

        int cid = toInt(segs[2], -1);
        if (cid <= 0) return errRes(400, "invalid contest id");

        if (segs.size() == 3) {
            if (method == "GET") {
                clijudge::contest::ContestStore store(g_dataDir);
                json c = store.view(cid);
                if (c.is_null()) return errRes(404, "contest not found");
                json problems = json::array();
                clijudge::problem::ProblemStore ps(g_dataDir);
                if (c.contains("problem_ids") && c["problem_ids"].is_array()) {
                    int idx = 1;
                    for (const auto& v : c["problem_ids"]) {
                        int pid = v.is_number_integer() ? v.get<int>() : 0;
                        json p = ps.view(pid);
                        std::string title;
                        if (!p.is_null() && p.contains("problem")) {
                            title = clijudge::settings::jsonValue<std::string>(p["problem"], "title", "");
                        }
                        problems.push_back({{"index", idx++}, {"problem_id", pid}, {"title", title}});
                    }
                }
                c["problems"] = problems;
                return jsonRes(c);
            }
            if (method == "DELETE") {
                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::contest::ContestStore store(g_dataDir);
                if (!store.deleteContest(cid)) return errRes(404, "contest not found");
                return okRes({{"id", cid}});
            }
            return errRes(405, "method not allowed");
        }

        if (segs.size() == 4 && segs[3] == "leaderboard" && method == "GET") {
            clijudge::contest::ContestStore store(g_dataDir);
            if (store.view(cid).is_null()) return errRes(404, "contest not found");
            return jsonRes({{"ok", true}, {"items", store.leaderboard(cid)}});
        }

        if (segs.size() == 4 && segs[3] == "report" && method == "GET") {
            clijudge::contest::ContestStore store(g_dataDir);
            std::string html = store.reportHtml(cid);
            if (html.empty()) return errRes(404, "contest not found");
            clijudge::gui::HttpResponse res;
            res.status = 200;
            res.contentType = "text/html; charset=utf-8";
            res.body = std::move(html);
            return res;
        }

        if (segs.size() >= 6 && segs[3] == "problems") {
            int idx = toInt(segs[4], -1);
            if (idx <= 0) return errRes(400, "invalid problem index");

            if (segs.size() == 6 && segs[5] == "submissions" && method == "GET") {
                clijudge::contest::ContestStore store(g_dataDir);
                if (store.view(cid).is_null()) return errRes(404, "contest not found");
                return jsonRes({{"ok", true}, {"items", store.viewProblemSubmissions(cid, idx)}});
            }

            if (segs.size() == 6 && segs[5] == "submit" && method == "POST") {
                json body;
                try { body = json::parse(req.body.empty() ? "{}" : req.body); }
                catch (...) { return errRes(400, "invalid json body"); }
                std::string filePath = resolveSubmissionSource(body);
                if (filePath.empty()) return errRes(400, "code or file_path is required");
                if (!fs::exists(filePath)) return errRes(400, "source file not found: " + filePath);
                std::string username = clijudge::settings::jsonValue<std::string>(body, "username", "");

                ScopedDataLock lk;
                if (!lk.ok()) return errRes(503, "data directory is locked by another process");
                clijudge::contest::ContestStore store(g_dataDir);
                int problemId = store.getContestProblemId(cid, idx);
                if (problemId < 0) return errRes(404, "contest or problem index not found");
                if (!store.submitProblem(cid, idx, filePath, username)) {
                    return errRes(500, "failed to submit contest problem");
                }
                json subs = store.viewProblemSubmissions(cid, idx);
                json last = (!subs.is_null() && subs.is_array() && !subs.empty())
                    ? subs.back() : json::object();
                return jsonRes({{"ok", true}, {"submission", last}, {"file_path", filePath}});
            }
            return errRes(404, "unknown contest problem route");
        }

        return errRes(404, "unknown contest route");
    }

    // ── 提交记录 ────────────────────────────────────────────
    if (segs.size() >= 2 && segs[1] == "submissions") {
        if (segs.size() == 2 && method == "GET") {
            int l = toInt(req.query.count("l") ? req.query.at("l") : "1", 1);
            int r = toInt(req.query.count("r") ? req.query.at("r") : "1000", 1000);
            clijudge::submit::SubmitStore store(g_dataDir);
            return jsonRes({
                {"ok", true}, {"total", store.count()},
                {"items", store.list(l < 1 ? 1 : l, r)}
            });
        }

        int sid = toInt(segs[2], -1);
        if (sid <= 0) return errRes(400, "invalid submission id");

        if (segs.size() == 3 && method == "GET") {
            clijudge::submit::SubmitStore store(g_dataDir);
            json sub = store.getSubmission(sid);
            if (sub.is_null()) return errRes(404, "submission not found");
            return jsonRes(sub);
        }

        if (segs.size() == 4 && segs[3] == "rejudge" && method == "POST") {
            ScopedDataLock lk;
            if (!lk.ok()) return errRes(503, "data directory is locked by another process");
            clijudge::submit::SubmitStore sstore(g_dataDir);
            json sub = sstore.getSubmission(sid);
            if (sub.is_null()) return errRes(404, "submission not found");

            int problemId = clijudge::settings::jsonValue<int>(sub, "problem_id", 0);
            std::string filePath = clijudge::settings::jsonValue<std::string>(sub, "file_path", "");
            int judgeTimes = clijudge::settings::jsonValue<int>(sub, "judge_times", 1);
            int maxTimes = clijudge::settings::getMaxRejudgeTimes();
            if (judgeTimes > maxTimes) {
                return errRes(400, "max rejudge times reached (" + std::to_string(maxTimes) + ")");
            }
            if (!fs::exists(filePath)) return errRes(400, "source file not found: " + filePath);

            clijudge::problem::ProblemStore pstore(g_dataDir);
            if (pstore.view(problemId).is_null()) return errRes(404, "problem not found");

            auto result = pstore.submit(problemId, filePath);
            json upd = {
                {"status", result.status}, {"score", result.score},
                {"time_used", result.timeUsed}, {"memory_used", result.memoryUsed},
                {"judge_times", judgeTimes + 1},
                {"judged_at", clijudge::submit::getCurrentTime()}
            };
            json detail = judgeDetailOf(result.judgeDetail);
            if (!detail.is_null()) upd["judge_detail"] = detail;
            sstore.updateSubmission(sid, upd);

            json out = {{"ok", true}, {"submission_id", sid}, {"status", result.status},
                        {"score", result.score}, {"time_used", result.timeUsed},
                        {"memory_used", result.memoryUsed}, {"judge_times", judgeTimes + 1}};
            if (!detail.is_null()) out["judge_detail"] = detail;
            return jsonRes(out);
        }

        return errRes(404, "unknown submission route");
    }

    return errRes(404, "unknown api");
}

clijudge::gui::HttpResponse handleRequest(const clijudge::gui::HttpRequest& req) {
    if (req.path.size() >= 5 && req.path.compare(0, 5, "/api/") == 0) {
        return handleApi(req);
    }
    if (req.method != "GET" && req.method != "HEAD") {
        return errRes(405, "method not allowed");
    }
    return serveAsset(req);
}

// ── 启动辅助 ───────────────────────────────────────────────
void openBrowser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
    std::system(cmd.c_str());
#endif
}

#ifndef _WIN32
void onSignal(int) { g_running = false; }
#endif

} // namespace

int main(int argc, char* argv[]) {
    clijudge::platform::initConsoleUtf8();

    g_cfg = clijudge::gui::loadGuiConfig();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            g_cfg.port = toInt(argv[++i], g_cfg.port);
        } else if (a == "--host" && i + 1 < argc) {
            g_cfg.host = argv[++i];
        } else if (a == "--data-dir" && i + 1 < argc) {
            g_cfg.dataDir = argv[++i];
        } else if (a == "--no-browser") {
            g_cfg.openBrowser = false;
        } else if (a == "--help" || a == "-h") {
            std::cout << "clijudgepp - CliJudge GUI (local web UI)\n"
                      << "\n"
                      << "Usage: clijudgepp [options]\n"
                      << "  --host <addr>       listen address (default 127.0.0.1)\n"
                      << "  --port <port>       listen port (default 8787, auto-increment if busy)\n"
                      << "  --data-dir <path>   shared data directory (default: auto detect)\n"
                      << "  --no-browser        do not open the browser\n"
                      << "\n"
                      << "Config file: gui.json next to the executable\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << a << " (use --help)\n";
            return 1;
        }
    }

    g_dataDir = clijudge::gui::resolveDataDir(g_cfg);
    std::error_code ec;
    fs::create_directories(g_dataDir, ec);
    if (!fs::is_directory(g_dataDir, ec)) {
        std::cerr << "failed to create data directory: " << g_dataDir << "\n";
        return 1;
    }
    // 与 CLI 一致：旧版平铺数据迁移（幂等）
    clijudge::platform::migrateFlatLayout(g_dataDir);

#ifndef _WIN32
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);
#endif

    clijudge::gui::HttpServer server;
    int port = server.bindAndListen(g_cfg.host, g_cfg.port);
    if (port < 0) {
        std::cerr << "failed to bind " << g_cfg.host << ":" << g_cfg.port
                  << " (ports " << g_cfg.port << "-" << (g_cfg.port + 19)
                  << " all unavailable)\n";
        return 1;
    }

    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::cout << "CliJudge++ GUI v" << kVersion << "\n"
              << "  data dir : " << g_dataDir << "\n"
              << "  url      : " << url << "\n"
              << "  stop     : Ctrl+C\n";
    std::cout.flush();

    if (g_cfg.openBrowser) openBrowser(url);

    server.serve(handleRequest, &g_running);
    std::cout << "server stopped\n";
    return 0;
}
