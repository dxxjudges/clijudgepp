// test_main.cpp
// 轻量单元测试（无第三方框架）：Markdown 纯文本抽取 + 比较模式表一致性。
// 由 CMake 的 clijudgepp_tests 目标构建，CI 中通过 ctest 运行。

#include <cstdio>
#include <string>

#include "compare_modes.h"
#include "judge.h"
#include "markdown.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

namespace {

void testMarkdownPlainText() {
    const std::string src =
        "# 标题一\n\n"
        "普通 **粗体** 与 *斜体* 以及 `inline code`。\n\n"
        "- 项目一\n- 项目二\n\n"
        "1. 有序一\n2. 有序二\n\n"
        "> 引用内容\n\n"
        "```cpp\nint x = 1;\n```\n\n"
        "| 列A | 列B |\n| --- | --- |\n| 1 | 2 |\n\n"
        "[链接文字](https://example.com)\n";

    const std::string plain = clijudge::gui::md::ToPlainText(src);
    CHECK(plain.find("标题一") != std::string::npos);
    CHECK(plain.find("粗体") != std::string::npos);
    CHECK(plain.find("斜体") != std::string::npos);
    CHECK(plain.find("inline code") != std::string::npos);
    CHECK(plain.find("项目一") != std::string::npos);
    CHECK(plain.find("有序二") != std::string::npos);
    CHECK(plain.find("引用内容") != std::string::npos);
    CHECK(plain.find("int x = 1;") != std::string::npos);
    CHECK(plain.find("列A") != std::string::npos);
    CHECK(plain.find("链接文字") != std::string::npos);

    // 标记符号不应残留在纯文本中
    CHECK(plain.find("**") == std::string::npos);
    CHECK(plain.find("```") == std::string::npos);
    CHECK(plain.find("](") == std::string::npos);
}

void testCompareModes() {
    using clijudge::gui::compareModeIndex;
    using clijudge::gui::kCompareModes;
    using clijudge::gui::kCompareModeCount;

    // 与 judge.h 的能力对齐：9 种模式全部可识别
    const char* expected[] = {"text_strict", "text_line", "text_no_space", "float_abs",
                              "float_rel",   "float_all", "spj",           "spj_lemon",
                              "spj_testlib"};
    CHECK(kCompareModeCount == (int)(sizeof(expected) / sizeof(expected[0])));
    for (const char* m : expected) {
        CHECK(compareModeIndex(m) >= 0);
    }

    // judge.h 认为是 SPJ 的模式，GUI 必须能表达
    for (const char* m : expected) {
        if (clijudge::judge::isSpecialJudgeMode(m)) CHECK(compareModeIndex(m) >= 0);
    }

    // 未知模式返回 -1（调用方保留原值，不得静默降级）
    CHECK(compareModeIndex("unknown_mode") == -1);
    CHECK(compareModeIndex("") == -1);
}

} // namespace

int main() {
    testMarkdownPlainText();
    testCompareModes();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", g_failures);
    return 1;
}
