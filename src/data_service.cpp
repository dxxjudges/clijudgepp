// data_service.cpp
#include "data_service.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>

#include "platform.h"
#include "problem.h"
#include "article.h"
#include "contest.h"
#include "submit.h"
#include "judge.h"

namespace clijudge {
namespace gui {

namespace fs = std::filesystem;

namespace {

// 修改类操作：与 CLI 使用同一把跨进程文件锁
class ScopedLock {
public:
    explicit ScopedLock(const std::string& dataDir) {
        locked_ = lock_.lock(platform::pathJoin(dataDir, ".clijudge.lock"));
    }
    ~ScopedLock() { lock_.unlock(); }

private:
    platform::DataLock lock_;
    bool locked_ = false;
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
    ScopedLock guard(dir_);
    problem::ProblemStore store(dir_);
    return store.edit(id, fieldsToUpdates(fields));
}

int DataService::createProblem(const std::string& title) {
    ScopedLock guard(dir_);
    problem::ProblemStore store(dir_);
    return store.create(title);
}

bool DataService::deleteProblem(int id) {
    ScopedLock guard(dir_);
    problem::ProblemStore store(dir_);
    return store.deleteProblem(id);
}

int DataService::addTestCase(int problemId, const std::string& input, const std::string& output,
                             int score, int timeLimit, int memoryLimit) {
    ScopedLock guard(dir_);
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
    ScopedLock guard(dir_);
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
    ScopedLock guard(dir_);
    article::ArticleStore store(dir_);
    return store.create(title, "", content);
}

bool DataService::deleteArticle(int id) {
    ScopedLock guard(dir_);
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
    ScopedLock guard(dir_);
    contest::ContestStore store(dir_);
    return store.create(title, start, end, problemIds);
}

bool DataService::deleteContest(int id) {
    ScopedLock guard(dir_);
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

        ScopedLock guard(dir);
        problem::ProblemStore ps(dir);
        json pv = ps.view(problemId);
        if (pv.is_null()) {
            out.error = "题目不存在";
            return out;
        }

        judge::JudgeResult jr = judge::judgeSubmission(pv, path);

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

        ScopedLock guard(dir);
        contest::ContestStore cs(dir);
        json before = cs.viewProblemSubmissions(contestId, problemIndex);
        if (!cs.submitProblem(contestId, problemIndex, path, username)) {
            out.error = "比赛题目提交失败";
            return out;
        }
        json after = cs.viewProblemSubmissions(contestId, problemIndex);
        if (after.is_array() && after.size() > (before.is_array() ? before.size() : 0)) {
            const json& rec = after.back();
            out.ok = true;
            out.status = rec.value("result", "");
            out.score = rec.value("score", 0);
            out.timeMs = rec.value("time_used", 0);
            out.memoryKb = rec.value("memory_used", 0);
            if (rec.contains("judge_detail")) out.detailJson = rec["judge_detail"].dump();
        }
        return out;
    });
}

std::future<JudgeOutcome> DataService::rejudgeAsync(int submissionId) {
    std::string dir = dir_;
    return std::async(std::launch::async, [dir, submissionId]() {
        JudgeOutcome out;
        ScopedLock guard(dir);
        submit::SubmitStore ss(dir);
        json sub = ss.getSubmission(submissionId);
        if (sub.is_null()) {
            out.error = "提交记录不存在";
            return out;
        }
        std::string path = sub.value("file_path", "");
        int pid = sub.value("problem_id", 0);
        if (!fs::exists(path)) {
            out.error = "源码文件不存在: " + path;
            return out;
        }
        problem::ProblemStore ps(dir);
        json pv = ps.view(pid);
        if (pv.is_null()) {
            out.error = "题目不存在";
            return out;
        }

        judge::JudgeResult jr = judge::judgeSubmission(pv, path);
        int times = sub.value("judge_times", 1) + 1;
        json updates = {
            {"status", judge::statusToAbbr(jr.status)},
            {"score", jr.totalScore},
            {"time_used", jr.totalTimeMs},
            {"memory_used", jr.maxMemoryKB},
            {"judge_times", times},
            {"judge_detail", judge::resultToJson(jr)}
        };
        ss.updateSubmission(submissionId, updates);

        out = outcomeFromResult(jr);
        out.submissionId = submissionId;
        return out;
    });
}

} // namespace gui
} // namespace clijudge
