// data_service.h
// GUI 数据服务：直接复用 clijudge/include 的 Store 与评测层。
// 所有修改类操作持有与 CLI 相同的跨进程数据锁（.clijudge.lock）。

#ifndef CLIJUDGE_GUI_DATA_SERVICE_H
#define CLIJUDGE_GUI_DATA_SERVICE_H

#include <future>
#include <string>
#include <vector>
#include "json.hpp"

namespace clijudge {
namespace gui {

using json = nlohmann::json;

struct JudgeOutcome {
    bool ok = false;
    std::string error;
    int submissionId = 0;
    std::string status;
    int score = 0;
    int maxScore = 0;
    int timeMs = 0;
    int memoryKb = 0;
    std::string detailJson;   // judge_detail
};

// 可提交语言：内置语言 + config.json 的 judge.custom_languages
// （由 judge.h 的编译分发能力决定，不再硬编码不支持的扩展名）
struct LangOption {
    std::string name;   // 显示名（自定义语言用配置里的键名）
    std::string ext;    // 扩展名，含点，如 ".cpp"
    std::string tmpl;   // 插入模板（自定义语言为空）
};

class DataService {
public:
    explicit DataService(std::string dataDir) : dir_(std::move(dataDir)) {}

    const std::string& dir() const { return dir_; }

    // 与 judge.h 的编译分发保持一致的语言清单
    std::vector<LangOption> languages() const;

    int countProblems();
    int countArticles();
    int countContests();
    int countSubmissions();

    json problemSummaries(int max = 2000);
    json problemView(int id);
    json problemTestCases(int id);
    bool saveProblem(int id, const json& fields);
    int createProblem(const std::string& title);
    bool deleteProblem(int id);
    int addTestCase(int problemId, const std::string& input, const std::string& output,
                    int score, int timeLimit, int memoryLimit);
    bool deleteTestCase(int problemId, int testCaseId);

    json articleList(int max = 500);
    json articleView(int id);
    int createArticle(const std::string& title, const std::string& content);
    bool deleteArticle(int id);

    json contestList();
    json contestView(int id);
    int createContest(const std::string& title, const std::string& start,
                      const std::string& end, const std::vector<int>& problemIds);
    bool deleteContest(int id);
    json contestLeaderboard(int id);
    json contestProblemSubmissions(int contestId, int index);

    json submissionList(int max = 500);
    json submissionGet(int id);

    // 异步评测（仅桌面端）
    std::future<JudgeOutcome> submitProblemAsync(int problemId, std::string source,
                                                 std::string ext, std::string username);
    std::future<JudgeOutcome> submitContestAsync(int contestId, int problemIndex,
                                                 std::string source, std::string ext,
                                                 std::string username);
    std::future<JudgeOutcome> rejudgeAsync(int submissionId);

    std::string writeSourceFile(const std::string& source, const std::string& ext) const;

private:
    std::string dir_;
};

} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_DATA_SERVICE_H
