// markdown.cpp
#include "markdown.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace clijudge {
namespace gui {
namespace md {
namespace {

enum class Align { Left, Center, Right };

struct Span {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool image = false;
    std::string link;
};

enum class BlockType { Paragraph, Heading, List, Code, Table, Quote, Hr };

struct Block {
    BlockType type = BlockType::Paragraph;
    int level = 0;          // 标题级别 / 有序列表编号
    bool ordered = false;
    std::vector<Span> spans;
    std::string code;
    std::vector<std::vector<std::string>> rows;
    std::vector<Align> aligns;
};

// ── 基础字符串工具 ────────────────────────────────────────
bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool isBlank(const std::string& s) {
    for (char c : s) {
        if (!std::isspace((unsigned char)c)) return false;
    }
    return true;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

int utf8Len(unsigned char b) {
    if (b >= 0xF0) return 4;
    if (b >= 0xE0) return 3;
    if (b >= 0xC0) return 2;
    return 1;
}

// 供换行使用的 token：ASCII 单词 / 空白段 / 单个非 ASCII 字符
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            size_t j = i;
            if (std::isspace(c)) {
                while (j < s.size() && (unsigned char)s[j] < 0x80 &&
                       std::isspace((unsigned char)s[j])) ++j;
            } else {
                while (j < s.size() && (unsigned char)s[j] < 0x80 &&
                       !std::isspace((unsigned char)s[j])) ++j;
            }
            out.push_back(s.substr(i, j - i));
            i = j;
        } else {
            int n = utf8Len(c);
            if (i + (size_t)n > s.size()) n = (int)(s.size() - i);
            out.push_back(s.substr(i, (size_t)n));
            i += (size_t)n;
        }
    }
    return out;
}

// ── 行内解析 ─────────────────────────────────────────────
void pushText(std::vector<Span>& out, const std::string& text,
              bool bold, bool italic, bool code, const std::string& link, bool image) {
    if (text.empty()) return;
    if (!out.empty()) {
        Span& last = out.back();
        if (last.bold == bold && last.italic == italic && last.code == code &&
            last.link == link && last.image == image) {
            last.text += text;
            return;
        }
    }
    Span sp;
    sp.text = text;
    sp.bold = bold;
    sp.italic = italic;
    sp.code = code;
    sp.link = link;
    sp.image = image;
    out.push_back(std::move(sp));
}

void parseInline(const std::string& s, std::vector<Span>& out) {
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];

        if (c == '\\' && i + 1 < s.size()) {
            pushText(out, std::string(1, s[i + 1]), false, false, false, "", false);
            i += 2;
            continue;
        }

        if (c == '`') {
            size_t end = s.find('`', i + 1);
            if (end != std::string::npos) {
                pushText(out, s.substr(i + 1, end - i - 1), false, false, true, "", false);
                i = end + 1;
                continue;
            }
        }

        if (c == '*' && i + 1 < s.size() && s[i + 1] == '*') {
            size_t end = s.find("**", i + 2);
            if (end != std::string::npos) {
                std::vector<Span> inner;
                parseInline(s.substr(i + 2, end - i - 2), inner);
                for (auto& sp : inner) sp.bold = true;
                for (auto& sp : inner) pushText(out, sp.text, sp.bold, sp.italic, sp.code, sp.link, sp.image);
                i = end + 2;
                continue;
            }
        }

        if (c == '~' && i + 1 < s.size() && s[i + 1] == '~') {
            size_t end = s.find("~~", i + 2);
            if (end != std::string::npos) {
                pushText(out, s.substr(i + 2, end - i - 2), false, true, false, "", false);
                i = end + 2;
                continue;
            }
        }

        if ((c == '*' || c == '_') && i + 1 < s.size() && !std::isspace((unsigned char)s[i + 1])) {
            size_t end = s.find(c, i + 1);
            if (end != std::string::npos && end > i + 1) {
                std::vector<Span> inner;
                parseInline(s.substr(i + 1, end - i - 1), inner);
                for (auto& sp : inner) sp.italic = true;
                for (auto& sp : inner) pushText(out, sp.text, sp.bold, sp.italic, sp.code, sp.link, sp.image);
                i = end + 1;
                continue;
            }
        }

        // 图片 ![alt](url)
        if (c == '!' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t close = s.find(']', i + 2);
            if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
                size_t rp = s.find(')', close + 2);
                if (rp != std::string::npos) {
                    std::string alt = s.substr(i + 2, close - i - 2);
                    std::string url = trim(s.substr(close + 2, rp - close - 2));
                    pushText(out, "[图片] " + (alt.empty() ? url : alt),
                             false, false, false, url, true);
                    i = rp + 1;
                    continue;
                }
            }
        }

        // 链接 [text](url)
        if (c == '[') {
            size_t close = s.find(']', i + 1);
            if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
                size_t rp = s.find(')', close + 2);
                if (rp != std::string::npos) {
                    std::string label = s.substr(i + 1, close - i - 1);
                    std::string url = trim(s.substr(close + 2, rp - close - 2));
                    std::vector<Span> inner;
                    parseInline(label, inner);
                    if (inner.empty()) inner.push_back(Span{label, false, false, false, false, ""});
                    for (auto& sp : inner) {
                        sp.link = url;
                        pushText(out, sp.text, sp.bold, sp.italic, sp.code, sp.link, false);
                    }
                    i = rp + 1;
                    continue;
                }
            }
        }

        // 普通文本
        size_t j = i;
        while (j < s.size()) {
            char ch = s[j];
            if (ch == '\\' || ch == '`' || ch == '*' || ch == '_' || ch == '~' ||
                ch == '[' || ch == '!') break;
            ++j;
        }
        if (j == i) ++j;
        pushText(out, s.substr(i, j - i), false, false, false, "", false);
        i = j;
    }
}

std::string flattenInline(const std::string& s) {
    std::vector<Span> spans;
    parseInline(s, spans);
    std::string out;
    for (auto& sp : spans) out += sp.text;
    return out;
}

// ── 表格 ─────────────────────────────────────────────────
std::vector<std::string> splitTableRow(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    bool esc = false;
    for (char c : line) {
        if (esc) {
            cur.push_back(c);
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '|') {
            cells.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    cells.push_back(trim(cur));
    // 去掉首尾空单元格（行首/行尾的竖线）
    if (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
    if (!cells.empty() && cells.back().empty()) cells.pop_back();
    return cells;
}

bool isTableSeparator(const std::string& line) {
    std::string t = trim(line);
    if (t.find('|') == std::string::npos) return false;
    bool hasDash = false;
    for (char c : t) {
        if (c == '-') hasDash = true;
        else if (c == '|' || c == ':' || std::isspace((unsigned char)c)) continue;
        else return false;
    }
    return hasDash;
}

Align cellAlign(const std::string& cell) {
    std::string t = trim(cell);
    bool left = !t.empty() && t.front() == ':';
    bool right = !t.empty() && t.back() == ':';
    if (left && right) return Align::Center;
    if (right) return Align::Right;
    return Align::Left;
}

// ── 解析 ─────────────────────────────────────────────────
std::vector<Block> parse(const std::string& text) {
    std::vector<Block> blocks;
    std::vector<std::string> lines = splitLines(text);
    size_t i = 0;

    while (i < lines.size()) {
        std::string raw = lines[i];
        std::string t = trim(raw);

        if (isBlank(t)) { ++i; continue; }

        // 代码块
        if (startsWith(t, "```")) {
            Block b;
            b.type = BlockType::Code;
            b.code.clear();
            ++i;
            while (i < lines.size() && !startsWith(trim(lines[i]), "```")) {
                if (!b.code.empty()) b.code += "\n";
                b.code += lines[i];
                ++i;
            }
            if (i < lines.size()) ++i;
            blocks.push_back(std::move(b));
            continue;
        }

        // 标题
        if (t[0] == '#') {
            int level = 0;
            while (level < (int)t.size() && t[level] == '#' && level < 6) ++level;
            if (level > 0 && (level == (int)t.size() || std::isspace((unsigned char)t[level]))) {
                Block b;
                b.type = BlockType::Heading;
                b.level = level;
                parseInline(trim(t.substr((size_t)level)), b.spans);
                blocks.push_back(std::move(b));
                ++i;
                continue;
            }
        }

        // 分隔线
        if ((startsWith(t, "---") || startsWith(t, "***") || startsWith(t, "___")) &&
            t.find_first_not_of("-*_ \t") == std::string::npos && t.size() >= 3) {
            Block b;
            b.type = BlockType::Hr;
            blocks.push_back(std::move(b));
            ++i;
            continue;
        }

        // 表格
        if (i + 1 < lines.size() && raw.find('|') != std::string::npos &&
            isTableSeparator(lines[i + 1])) {
            Block b;
            b.type = BlockType::Table;
            std::vector<std::string> header = splitTableRow(raw);
            std::vector<std::string> sep = splitTableRow(lines[i + 1]);
            for (size_t c = 0; c < header.size(); ++c) {
                b.aligns.push_back(c < sep.size() ? cellAlign(sep[c]) : Align::Left);
            }
            b.rows.push_back(header);
            i += 2;
            while (i < lines.size() && lines[i].find('|') != std::string::npos &&
                   !isBlank(lines[i])) {
                b.rows.push_back(splitTableRow(lines[i]));
                ++i;
            }
            if (!b.rows.empty()) blocks.push_back(std::move(b));
            continue;
        }

        // 引用
        if (startsWith(t, ">")) {
            Block b;
            b.type = BlockType::Quote;
            while (i < lines.size() && startsWith(trim(lines[i]), ">")) {
                std::string content = trim(trim(lines[i]).substr(1));
                if (!b.spans.empty()) pushText(b.spans, " ", false, false, false, "", false);
                parseInline(content, b.spans);
                ++i;
            }
            blocks.push_back(std::move(b));
            continue;
        }

        // 列表
        {
            bool ordered = false;
            size_t offset = 0;
            int number = 0;
            if ((t[0] == '-' || t[0] == '*' || t[0] == '+') &&
                (t.size() > 1 && std::isspace((unsigned char)t[1]))) {
                offset = 1;
            } else {
                size_t d = 0;
                while (d < t.size() && std::isdigit((unsigned char)t[d])) ++d;
                if (d > 0 && d + 1 < t.size() && t[d] == '.' &&
                    std::isspace((unsigned char)t[d + 1])) {
                    ordered = true;
                    number = std::atoi(t.substr(0, d).c_str());
                    offset = d + 1;
                }
            }
            if (offset > 0) {
                Block b;
                b.type = BlockType::List;
                b.ordered = ordered;
                b.level = number;
                parseInline(trim(t.substr(offset)), b.spans);
                blocks.push_back(std::move(b));
                ++i;
                continue;
            }
        }

        // 段落（连续非空行合并）
        {
            Block b;
            b.type = BlockType::Paragraph;
            std::string joined;
            while (i < lines.size() && !isBlank(lines[i])) {
                std::string lt = trim(lines[i]);
                if (!joined.empty()) joined += " ";
                joined += lt;
                ++i;
            }
            parseInline(joined, b.spans);
            blocks.push_back(std::move(b));
        }
    }
    return blocks;
}

// ── 绘制 ─────────────────────────────────────────────────
struct Ctx {
    ImDrawList* dl = nullptr;
    const Theme* th = nullptr;
    std::vector<std::pair<ImVec2, ImVec2>> linkRects;
    std::vector<std::string> linkUrls;
};

float measure(const Theme& th, float size, const std::string& s) {
    if (!th.font || s.empty()) return 0.0f;
    return th.font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()).x;
}

bool isSpaceToken(const std::string& t) {
    for (char c : t) {
        if (!std::isspace((unsigned char)c)) return false;
    }
    return !t.empty();
}

// 返回结束 y
float drawInline(Ctx& c, const std::vector<Span>& spans, float x0, float y,
                 float size, float wrapRight) {
    const Theme& th = *c.th;
    float lineH = size * 1.42f;
    float x = x0;

    for (const Span& sp : spans) {
        ImU32 col = th.text;
        if (sp.code) col = th.codeText;
        if (!sp.link.empty() || sp.image) col = sp.image ? th.muted : th.link;
        if (sp.bold) col = IM_COL32(247, 250, 255, 255);

        for (const auto& tok : tokenize(sp.text)) {
            float w = measure(th, size, tok);
            bool space = isSpaceToken(tok);
            if (space && x == x0) continue;
            if (!space && x + w > wrapRight && x > x0) {
                y += lineH;
                x = x0;
            }
            if (sp.code && !space) {
                c.dl->AddRectFilled(ImVec2(x - 2.0f, y + 1.0f),
                                    ImVec2(x + w + 2.0f, y + lineH - 1.0f),
                                    th.codeBg, 3.0f);
            }
            c.dl->AddText(th.font, size, ImVec2(x, y), col, tok.c_str());
            if (!sp.link.empty()) {
                c.dl->AddLine(ImVec2(x, y + size * 1.16f),
                              ImVec2(x + w, y + size * 1.16f), th.link, 1.0f);
                c.linkRects.push_back({ImVec2(x, y), ImVec2(x + w, y + lineH)});
                c.linkUrls.push_back(sp.link);
            }
            x += w;
        }
    }
    return y + lineH;
}

std::vector<std::string> wrapCell(const Theme& th, float size, const std::string& text, float width) {
    std::vector<std::string> lines;
    std::vector<std::string> toks = tokenize(text);
    std::string cur;
    float curW = 0.0f;
    for (const auto& tok : toks) {
        float w = measure(th, size, tok);
        bool space = isSpaceToken(tok);
        if (!space && curW + w > width && !cur.empty()) {
            lines.push_back(trim(cur));
            cur.clear();
            curW = 0.0f;
        }
        if (space && cur.empty()) continue;
        cur += tok;
        curW += w;
    }
    if (!cur.empty()) lines.push_back(trim(cur));
    if (lines.empty()) lines.push_back("");
    return lines;
}

void drawTable(Ctx& c, const Block& b, float x0, float y, float width,
               float size, float& outBottom) {
    const Theme& th = *c.th;
    if (b.rows.empty()) { outBottom = y; return; }

    size_t cols = 0;
    for (const auto& row : b.rows) cols = std::max(cols, row.size());
    if (cols == 0) { outBottom = y; return; }

    float pad = 8.0f;
    float lineH = size * 1.42f;
    const float minCol = 48.0f;

    std::vector<float> colW(cols, minCol);
    std::vector<float> natural(cols, minCol);
    for (const auto& row : b.rows) {
        for (size_t ci = 0; ci < row.size(); ++ci) {
            float w = measure(th, size, flattenInline(row[ci])) + pad * 2.0f;
            natural[ci] = std::max(natural[ci], std::min(w, width * 0.6f));
        }
    }
    float total = 0.0f;
    for (float w : natural) total += w;

    if (total <= width) {
        colW = natural;
        float extra = (width - total) / (float)cols;
        for (size_t ci = 0; ci < cols; ++ci) colW[ci] += extra;
    } else {
        float each = std::max(minCol, width / (float)cols);
        for (size_t ci = 0; ci < cols; ++ci) colW[ci] = each;
    }

    float curY = y;
    for (size_t ri = 0; ri < b.rows.size(); ++ri) {
        const auto& row = b.rows[ri];
        std::vector<std::vector<std::string>> cellLines(cols);
        size_t maxLines = 1;
        for (size_t ci = 0; ci < cols; ++ci) {
            std::string text = ci < row.size() ? flattenInline(row[ci]) : "";
            cellLines[ci] = wrapCell(th, size, text, colW[ci] - pad * 2.0f);
            maxLines = std::max(maxLines, cellLines[ci].size());
        }
        float rowH = (float)maxLines * lineH + pad * 2.0f;

        if (ri == 0) {
            c.dl->AddRectFilled(ImVec2(x0, curY), ImVec2(x0 + width, curY + rowH),
                                th.tableBg, 4.0f);
        }
        float x = x0;
        for (size_t ci = 0; ci < cols; ++ci) {
            float inner = colW[ci] - pad * 2.0f;
            Align al = ci < b.aligns.size() ? b.aligns[ci] : Align::Left;
            ImU32 col = ri == 0 ? IM_COL32(238, 243, 250, 255) : th.text;
            for (size_t li = 0; li < cellLines[ci].size(); ++li) {
                const std::string& line = cellLines[ci][li];
                float w = measure(th, size, line);
                float tx = x + pad;
                if (al == Align::Center) tx = x + pad + (inner - w) * 0.5f;
                else if (al == Align::Right) tx = x + pad + (inner - w);
                c.dl->AddText(th.font, size,
                              ImVec2(tx, curY + pad + (float)li * lineH), col, line.c_str());
            }
            if (ci > 0) {
                c.dl->AddLine(ImVec2(x, curY), ImVec2(x, curY + rowH), th.border, 1.0f);
            }
            x += colW[ci];
        }
        c.dl->AddLine(ImVec2(x0, curY + rowH), ImVec2(x0 + width, curY + rowH),
                      th.border, 1.0f);
        curY += rowH;
    }
    outBottom = curY;
}

void drawCode(Ctx& c, const Block& b, float x0, float y, float width,
              float size, float& outBottom) {
    const Theme& th = *c.th;
    float pad = 10.0f;
    float lineH = size * 1.35f;
    std::vector<std::string> lines = splitLines(b.code);
    float height = (float)lines.size() * lineH + pad * 2.0f;

    c.dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + width, y + height), th.codeBg, 4.0f);
    c.dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + 3.0f, y + height), th.accent, 2.0f);
    c.dl->PushClipRect(ImVec2(x0, y), ImVec2(x0 + width, y + height), true);
    for (size_t li = 0; li < lines.size(); ++li) {
        c.dl->AddText(th.font, size,
                      ImVec2(x0 + pad, y + pad + (float)li * lineH),
                      th.codeText, lines[li].c_str());
    }
    c.dl->PopClipRect();
    outBottom = y + height;
}

} // namespace

void Render(const std::string& markdownText, const Theme& theme, float width,
            const LinkOpener& openLink) {
    if (!theme.font) return;
    float x0 = ImGui::GetCursorScreenPos().x;
    float y0 = ImGui::GetCursorScreenPos().y;
    float right = x0 + width;

    std::vector<Block> blocks = parse(markdownText);

    Ctx c;
    c.dl = ImGui::GetWindowDrawList();
    c.th = &theme;

    float y = y0;
    float base = theme.fontSize;
    float gap = theme.lineSpacing;

    for (const Block& b : blocks) {
        switch (b.type) {
            case BlockType::Heading: {
                float sizes[6] = {1.85f, 1.55f, 1.32f, 1.18f, 1.08f, 1.0f};
                int idx = std::min(std::max(b.level - 1, 0), 5);
                float size = base * sizes[idx];
                y = drawInline(c, b.spans, x0, y, size, right);
                if (b.level <= 2) {
                    c.dl->AddLine(ImVec2(x0, y - gap * 0.2f), ImVec2(right, y - gap * 0.2f),
                                  theme.border, 1.0f);
                }
                y += gap * (b.level <= 2 ? 2.0f : 1.2f);
                break;
            }
            case BlockType::Paragraph:
                y = drawInline(c, b.spans, x0, y, base, right);
                y += gap * 2.0f;
                break;
            case BlockType::List: {
                std::string marker = b.ordered ? (std::to_string(b.level) + ".") : "•";
                c.dl->AddText(theme.font, base, ImVec2(x0 + 4.0f, y), theme.accent, marker.c_str());
                float indent = base * 1.6f;
                y = drawInline(c, b.spans, x0 + indent, y, base, right);
                y += gap;
                break;
            }
            case BlockType::Quote: {
                float top = y;
                y = drawInline(c, b.spans, x0 + 14.0f, y, base, right);
                c.dl->AddRectFilled(ImVec2(x0 + 2.0f, top), ImVec2(x0 + 5.0f, y - gap),
                                    theme.quoteBar, 2.0f);
                y += gap * 2.0f;
                break;
            }
            case BlockType::Code: {
                float bottom = y;
                drawCode(c, b, x0, y, width, base * 0.95f, bottom);
                y = bottom + gap * 2.0f;
                break;
            }
            case BlockType::Table: {
                float bottom = y;
                drawTable(c, b, x0, y, width, base * 0.95f, bottom);
                y = bottom + gap * 2.0f;
                break;
            }
            case BlockType::Hr: {
                y += gap;
                c.dl->AddLine(ImVec2(x0, y), ImVec2(right, y), theme.border, 1.0f);
                y += gap * 2.0f;
                break;
            }
        }
    }

    float height = y - y0;
    if (height < 1.0f) height = 1.0f;
    ImGui::Dummy(ImVec2(width, height));

    // 链接交互
    bool clicked = ImGui::IsWindowHovered() && ImGui::IsMouseReleased(0);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    for (size_t li = 0; li < c.linkRects.size(); ++li) {
        const ImVec2& a = c.linkRects[li].first;
        const ImVec2& bb = c.linkRects[li].second;
        if (mouse.x >= a.x && mouse.x <= bb.x && mouse.y >= a.y && mouse.y <= bb.y) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clicked && openLink) {
                openLink(c.linkUrls[li]);
                break;
            }
        }
    }
}

std::string ToPlainText(const std::string& markdownText) {
    std::vector<Block> blocks = parse(markdownText);
    std::string out;
    for (const Block& b : blocks) {
        switch (b.type) {
            case BlockType::Heading: {
                std::string t;
                for (auto& sp : b.spans) t += sp.text;
                out += std::string((size_t)b.level, '#') + " " + t + "\n\n";
                break;
            }
            case BlockType::Paragraph: {
                for (auto& sp : b.spans) out += sp.text;
                out += "\n\n";
                break;
            }
            case BlockType::List: {
                for (auto& sp : b.spans) out += sp.text;
                out += "\n";
                break;
            }
            case BlockType::Quote: {
                out += "> ";
                for (auto& sp : b.spans) out += sp.text;
                out += "\n\n";
                break;
            }
            case BlockType::Code:
                out += b.code + "\n\n";
                break;
            case BlockType::Table:
                for (const auto& row : b.rows) {
                    for (size_t ci = 0; ci < row.size(); ++ci) {
                        if (ci) out += " | ";
                        out += flattenInline(row[ci]);
                    }
                    out += "\n";
                }
                out += "\n";
                break;
            case BlockType::Hr:
                out += "---\n\n";
                break;
        }
    }
    return out;
}

} // namespace md
} // namespace gui
} // namespace clijudge
