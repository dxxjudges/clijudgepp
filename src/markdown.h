// markdown.h
// 轻量 Markdown 解析 + ImGui 自绘渲染（无第三方依赖）
//
// 支持: 标题 / 段落 / 有序与无序列表 / 代码块 / 表格(对齐) / 引用 /
//       分隔线 / 粗体 / 斜体 / 行内代码 / 链接 / 图片占位 / 反斜杠转义
// 图片不下载真实位图，渲染为可点击的 "[图片] alt" 占位。

#ifndef CLIJUDGE_GUI_MARKDOWN_H
#define CLIJUDGE_GUI_MARKDOWN_H

#include <string>
#include <functional>
#include "imgui.h"

namespace clijudge {
namespace gui {
namespace md {

struct Theme {
    ImFont* font = nullptr;
    float fontSize = 18.0f;
    float lineSpacing = 4.0f;
    ImU32 text      = IM_COL32(222, 228, 238, 255);
    ImU32 muted     = IM_COL32(150, 160, 175, 255);
    ImU32 accent    = IM_COL32(96, 165, 250, 255);
    ImU32 link      = IM_COL32(125, 211, 252, 255);
    ImU32 codeBg    = IM_COL32(27, 33, 45, 255);
    ImU32 codeText  = IM_COL32(168, 216, 255, 255);
    ImU32 tableBg   = IM_COL32(34, 41, 54, 255);
    ImU32 border    = IM_COL32(66, 76, 92, 255);
    ImU32 quoteBar  = IM_COL32(96, 165, 250, 255);
};

using LinkOpener = std::function<void(const std::string& url)>;

// 在 ImGui 光标处渲染 Markdown，宽度为 width，并推进光标。
void Render(const std::string& markdownText, const Theme& theme, float width,
            const LinkOpener& openLink);

// 纯文本导出（自检/测试用）
std::string ToPlainText(const std::string& markdownText);

} // namespace md
} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_MARKDOWN_H
