// data_service.cpp
#include "data_service.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "platform.h"
#include "problem.h"
#include "article.h"
#include "contest.h"
#include "submit.h"
#include "judge.h"
#include "settings.h"

namespace clijudge {
namespace gui {

namespace fs = std::filesystem;

namespace {

// 进程内串行化写操作：GUI 内多个操作不会互相踩索引文件，
// 且不会与跨进程文件锁叠加造成自锁。
std::mutex& writeMutex() {
    static std::mutex m;
    return m;
}

// 修改类操作：与 CLI 使用同一把跨进程文件锁。
// 获取失败时 ok()==false，调用方必须放弃写入（与 CLI 的 fail-closed 一致）。
class ScopedLock {
public:
    explicit ScopedLock(const std::string& dataDir)
        : ok_(lock_.lock(platform::pathJoin(dataDir, ".clijudge.lock"))) {}
    ~ScopedLock() {
        if (ok_) lock_.unlock();
    }
    bool ok() const { return ok_; }

private:
    platform::DataLock lock_;
    bool ok_ = false;
};

// 写操作统一入口：进程内互斥 + 跨进程锁，任一失败即放弃
class WriteGuard {
public:
    explicit WriteGuard(const std::string& dataDir)
        : seq_(writeMutex()), lock_(dataDir) {}
    bool ok() const { return lock_.ok(); }

private:
    std::lock_guard<std::mutex> seq_;
    ScopedLock lock_;
};

std::string safeExt(const std::string& ext) {
    if (ext.size() >= 2 && ext.size() <= 8 && ext[0] == '.') {
        bool ok = true;
        for (size_t i = 1; i < ext.size(); ++i) {
            char c = ext[i];
            if (!(std::isalnum((unsigned char)c))) { ok = false; break; }
        }
        if (ok) return ext;
    }
    return ".cpp";
}

JudgeOutcome outcomeFromResult(const judge::JudgeResult& r) {
    JudgeOutcome out;
    out.ok = true;
    out.status = judge::statusToAbbr(r.status);
    out.score = r.totalScore;
    out.maxScore = r.maxScore;
    out.timeMs = r.totalTimeMs;
    out.memoryKb = r.maxMemoryKB;
    out.detailJson = judge::resultToJson(r).dump();
    return out;
}

json fieldsToUpdates(const json& fields) {
    json updates = json::object();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        updates[it.key()] = it.value();
    }
    return updates;
}

} // namespace

std::string DataService::writeSourceFile(const std::string& source, const std::string& ext) const {
    std::string dir = platform::pathJoin(dir_, "gui_src");
    std::error_code ec;
    fs::create_directories(dir, ec);
    static std::atomic<uint64_t> seq{0};
    std::string name = "sub_" + std::to_string(platform::pid()) + "_" +
                       std::to_string(platform::tickMs()) + "_" +
                       std::to_string(seq.fetch_add(1)) + safeExt(ext);
    std::string path = platform::pathJoin(dir, name);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return std::string();
    f.write(source.data(), (std::streamsize)source.size());
    f.close();
    return path;
}

std::vector<LangOption> DataService::languages() const {
    std::vector<LangOption> out;

    // 自定义语言优先（judge.h 的 compileSources 也是自定义扩展名覆盖内置）
    for (const auto& c : settings::get().customLanguages) {
        if (c.extensions.empty()) continue;
        out.push_back({c.name, c.extensions.front(), ""});
    }

    struct Builtin {
        const char* ext;
        const char* tmpl;
    };
    static const Builtin kBuiltin[] = {
        {".cpp",
         "#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    \n    return 0;\n}\n"},
        {".c", "#include <stdio.h>\n\nint main() {\n    \n    return 0;\n}\n"},
        {".java",
         "import java.util.*;\n\npublic class Main {\n    public static void main(String[] args) {\n"
         "        \n    }\n}\n"},
        {".py", "import sys\n\ndef main():\n    pass\n\nmain()\n"},
        {".js", "// Node.js\nconst fs = require('fs');\n\n"},
    };
    for (const auto& b : kBuiltin) {
        // 已被自定义语言覆盖的扩展名不重复列出
        if (settings::findByExtension(b.ext) != nullptr) continue;
        out.push_back({judge::getLanguageName(b.ext), b.ext, b.tmpl});
    }

    // 兜底：配置异常时至少保证能提交 C++
    if (out.empty()) out.push_back({judge::getLanguageName(".cpp"), ".cpp", kBuiltin[0].tmpl});
    return out;
}

int DataService::countProblems() {
    problem::ProblemStore store(dir_);
    return store.count();
}

int DataService::countArticles() {
    article::ArticleStore store(dir_);
    return store.count();
}

int DataService::countContests() {
    contest::ContestStore store(dir_);
    return (int)store.list().size();
}

int DataService::countSubmissions() {
    submit::SubmitStore store(dir_);
    return store.count();
}

json DataService::problemSummaries(int max) {
    problem::ProblemStore store(dir_);
    return store.list(1, max);
}

json DataService::problemView(int id) {
    problem::ProblemStore store(dir_);
    return store.view(id);
}

json DataService::problemTestCases(int id) {
    json pv = problemView(id);
    if (pv.is_null() || !pv.contains("test_cases")) return json::array();
    return pv["test_cases"];
}

bool DataService::saveProblem(int id, const json& fields) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return false;
    problem::ProblemStore store(dir_);
    return store.edit(id, fieldsToUpdates(fields));
}

int DataService::createProblem(const std::string& title) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return 0;
    problem::ProblemStore store(dir_);
    return store.create(title);
}

bool DataService::deleteProblem(int id) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return false;
    problem::ProblemStore store(dir_);
    return store.deleteProblem(id);
}

int DataService::addTestCase(int problemId, const std::string& input, const std::string& output,
                             int score, int timeLimit, int memoryLimit) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return 0;
    problem::ProblemStore store(dir_);
    json pv = store.view(problemId);
    if (pv.is_null()) return 0;

    int nextId = 1;
    if (pv.contains("test_cases") && pv["test_cases"].is_array()) {
        for (const auto& tc : pv["test_cases"]) {
            if (tc.contains("id") && tc["id"].is_number_integer()) {
                nextId = std::max(nextId, tc["id"].get<int>() + 1);
            }
        }
    }

    problem::TestCase tc{};
    tc.id = nextId;
    tc.inputData = input;
    tc.outputData = output;
    tc.inputFile = "";
    tc.outputFile = "";
    tc.score = score;
    tc.timeLimit = timeLimit;
    tc.memoryLimit = memoryLimit;
    tc.sortOrder = nextId;
    tc.subtaskId = 0;

    return store.addTestCase(problemId, tc) ? nextId : 0;
}

bool DataService::deleteTestCase(int problemId, int testCaseId) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return false;
    problem::ProblemStore store(dir_);
    return store.deleteTestCase(problemId, testCaseId);
}

json DataService::articleList(int max) {
    article::ArticleStore store(dir_);
    return store.list(1, max);
}

json DataService::articleView(int id) {
    article::ArticleStore store(dir_);
    return store.view(id);
}

int DataService::createArticle(const std::string& title, const std::string& content) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return 0;
    article::ArticleStore store(dir_);
    return store.create(title, "", content);
}

bool DataService::deleteArticle(int id) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return false;
    article::ArticleStore store(dir_);
    return store.deleteArticle(id);
}

json DataService::contestList() {
    contest::ContestStore store(dir_);
    return store.list();
}

json DataService::contestView(int id) {
    contest::ContestStore store(dir_);
    return store.view(id);
}

int DataService::createContest(const std::string& title, const std::string& start,
                               const std::string& end, const std::vector<int>& problemIds) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return 0;
    contest::ContestStore store(dir_);
    return store.create(title, start, end, problemIds);
}

bool DataService::deleteContest(int id) {
    WriteGuard guard(dir_);
    if (!guard.ok()) return false;
    contest::ContestStore store(dir_);
    return store.deleteContest(id);
}

json DataService::contestLeaderboard(int id) {
    contest::ContestStore store(dir_);
    return store.leaderboard(id);
}

json DataService::contestProblemSubmissions(int contestId, int index) {
    contest::ContestStore store(dir_);
    return store.viewProblemSubmissions(contestId, index);
}

json DataService::submissionList(int max) {
    submit::SubmitStore store(dir_);
    return store.list(1, max);
}

json DataService::submissionGet(int id) {
    submit::SubmitStore store(dir_);
    return store.getSubmission(id);
}

std::future<JudgeOutcome> DataService::submitProblemAsync(int problemId, std::string source,
                                                          std::string ext, std::string username) {
    std::string dir = dir_;
    return std::async(std::launch::async, [dir, problemId, source, ext, username]() {
        JudgeOutcome out;
        std::string path = DataService(dir).writeSourceFile(source, ext);
        if (path.empty()) {
            out.error = "无法写入源码文件";
            return out;
        }

        // 1) 短暂持锁读取题目快照
        json pv;
        {
            WriteGuard guard(dir);
            if (!guard.ok()) {
                out.error = "无法获取数据锁";
                return out;
            }
            problem::ProblemStore ps(dir);
            pv = ps.view(problemId);
        }
        if (pv.is_null()) {
            out.error = "题目不存在";
            return out;
        }

        // 2) 评测期间不持锁：否则 GUI 的写操作会被整段判题阻塞（界面冻结）
        judge::JudgeResult jr = judge::judgeSubmission(pv, path);

        // 3) 只在落盘提交记录时短暂持锁
        {
            WriteGuard guard(dir);
            if (!guard.ok()) {
                out.error = "无法获取数据锁，评测结果未保存";
                return out;
            }
            submit::Submission sub;
            sub.id = 0;
            sub.problemId = problemId;
            sub.problemTitle = pv.contains("problem") ? pv["problem"].value("title", "") : "";
            sub.filePath = path;
            sub.submittedAt = submit::getCurrentTime();
            sub.status = judge::statusToAbbr(jr.status);
            sub.score = jr.totalScore;
            sub.timeUsed = jr.totalTimeMs;
            sub.memoryUsed = jr.maxMemoryKB;
            sub.username = username.empty() ? "gui" : username;
            sub.judgeDetail = judge::resultToJson(jr).dump();

            submit::SubmitStore ss(dir);
            int sid = ss.addSubmission(sub);
            out = outcomeFromResult(jr);
            out.submissionId = sid;
        }
        return out;
    });
}

std::future<JudgeOutcome> DataService::submitContestAsync(int contestId, int problemIndex,
                                                          std::string source, std::string ext,
                                                          std::string username) {
    std::string dir = dir_;
    return std::async(std::launch::async, [dir, contestId, problemIndex, source, ext, username]() {
        JudgeOutcome out;
        std::string path = DataService(dir).writeSourceFile(source, ext);
        if (path.empty()) {
            out.error = "无法写入源码文件";
            return out;
        }

        // 1) 短暂持锁：解析比赛题目编号并读取题目快照
        int problemId = -1;
        json pv;
        {
            WriteGuard guard(dir);
            if (!guard.ok()) {
                out.error = "无法获取数据锁";
                return out;
            }
            contest::ContestStore cs(dir);
            problemId = cs.getContestProblemId(contestId, problemIndex);
            if (problemId < 0) {
                out.error = "比赛题目不存在";
                return out;
            }
            problem::ProblemStore ps(dir);
            pv = ps.view(problemId);
        }
        if (pv.is_null()) {
            out.error = "题目不存在";
            return out;
        }

        // 2) 评测期间不持锁
        judge::JudgeResult jr = judge::judgeSubmission(pv, path);
        std::string abbr = judge::statusToAbbr(jr.status);

        // 3) 短暂持锁追加比赛提交记录（格式与 ContestStore::submitProblem 一致）
        {
            WriteGuard guard(dir);
            if (!guard.ok()) {
                out.error = "无法获取数据锁，评测结果未保存";
                return out;
            }
            json submission = {
                {"contest_id", contestId},
                {"problem_index", problemIndex},
                {"problem_id", problemId},
                {"file_path", path},
                {"submitted_at", submit::getCurrentTime()},
                {"result", abbr},
                {"score", jr.totalScore},
                {"time_used", jr.totalTimeMs},
                {"memory_used", jr.maxMemoryKB},
                {"judge_times", 1},
                {"username", username.empty() ? "gui" : username},
                {"judge_detail", judge::resultToJson(jr)}};

            std::error_code ec;
            fs::create_directories(platform::pathJoin(dir, "contests"), ec);
            std::string submissionsPath =
                platform::pathJoin(dir, "contests/contest_" + std::to_string(contestId) +
                                            "_problem_" + std::to_string(problemIndex) +
                                            "_submissions.json");
            json submissions = json::array();
            if (fs::exists(submissionsPath)) {
                std::ifstream f(submissionsPath);
                if (f.is_open()) {
                    try {
                        f >> submissions;
                    } catch (...) {
                        submissions = json::array();
                    }
                }
            }
            if (!submissions.is_array()) submissions = json::array();
            submissions.push_back(submission);
            if (!platform::writeFileAtomic(submissionsPath, submissions.dump(2))) {
                out.error = "写入比赛提交记录失败";
                return out;
            }

            out.ok = true;
            out.status = abbr;
            out.score = jr.totalScore;
            out.timeMs = jr.totalTimeMs;
            out.memoryKb = jr.maxMemoryKB;
            out.detailJson = judge::resultToJson(jr).dump();
        }
        return out;
    });
}

std::future<JudgeOutcome> DataService::rejudgeAsync(int submissionId) {
    std::string dir = dir_;
    return std::async(std::launch::async, [dir, submissionId]() {
        JudgeOutcome out;

        // 1) 短暂持锁读取提交记录与题目快照
        json sub, pv;
        std::string path;
        int pid = 0;
        {
            WriteGuard guard(dir);
            if (!guard.ok()) {
                out.error = "无法获取数据锁";
                return out;
            }
            submit::SubmitStore ss(dir);
            sub = ss.getSubmission(submissionId);
            if (sub.is_null()) {
                out.error = "提交记录不存在";
                return out;
            }
            path = sub.value("file_path", "");
            pid = sub.value("problem_id", 0);
            if (!fs::exists(path)) {
                out.error = "源码文件不存在: " + path;
                return out;
            }
            problem::ProblemStore ps(dir);
            pv = ps.view(pid);
        }
        if (pv.is_null()) {
            out.error = "题目不存在";
            return out;
        }

        // 2) 重判期间不持锁
        judge::JudgeResult jr = judge::judgeSubmission(pv, path);
        int times = sub.value("judge_times", 1) + 1;

        // 3) 短暂持锁写回结果
        {
            WriteGuard guard(dir);
            if (!guard.ok()) {
                out.error = "无法获取数据锁，重判结果未保存";
                return out;
            }
            json updates = {
                {"status", judge::statusToAbbr(jr.status)},
                {"score", jr.totalScore},
                {"time_used", jr.totalTimeMs},
                {"memory_used", jr.maxMemoryKB},
                {"judge_times", times},
                {"judge_detail", judge::resultToJson(jr)}};
            submit::SubmitStore ss(dir);
            ss.updateSubmission(submissionId, updates);

            out = outcomeFromResult(jr);
            out.submissionId = submissionId;
        }
        return out;
    });
}

} // namespace gui
} // namespace clijudge
