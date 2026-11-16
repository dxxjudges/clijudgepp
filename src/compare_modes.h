// compare_modes.h
// 题目比较模式唯一来源：与 clijudge/include/judge.h 支持的模式一一对应。
// 新增模式时只改这里（judge.h 的 compareOutputs / isSpecialJudgeMode 同步）。

#ifndef CLIJUDGE_GUI_COMPARE_MODES_H
#define CLIJUDGE_GUI_COMPARE_MODES_H

#include <string>

namespace clijudge {
namespace gui {

inline const char* const kCompareModes[] = {
    "text_strict", "text_line", "text_no_space", "float_abs",
    "float_rel",    "float_all", "spj",           "spj_lemon",
    "spj_testlib",
};
inline constexpr int kCompareModeCount = (int)(sizeof(kCompareModes) / sizeof(kCompareModes[0]));

// 未识别的模式返回 -1：调用方必须保留原值，不能回退成 text_strict
inline int compareModeIndex(const std::string& mode) {
    for (int i = 0; i < kCompareModeCount; ++i) {
        if (mode == kCompareModes[i]) return i;
    }
    return -1;
}

} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_COMPARE_MODES_H
