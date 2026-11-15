// main.cpp
// CliJudge++ 原生 GUI 入口（SDL2 + Dear ImGui）
//
// 桌面端: 单文件可执行程序，直接复用 ../main/include 的数据层与评测层。
// Android 端: SDLActivity 加载 libSDL2.so 后加载 libmain.so，仅支持管理/浏览。

#include <SDL.h>
#include <SDL_system.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "data_service.h"
#include "gui_config.h"
#include "markdown.h"
#include "ui_app.h"

namespace fs = std::filesystem;
using clijudge::gui::AppOptions;
using clijudge::gui::GuiConfig;
using clijudge::gui::UiApp;

namespace {

#ifdef __ANDROID__
constexpr bool kAndroid = true;
#else
constexpr bool kAndroid = false;
#endif

std::string platformDefaultDataDir() {
#ifdef __ANDROID__
    const char* p = SDL_AndroidGetInternalStoragePath();
    if (p && p[0]) return clijudge::platform::pathJoin(p, "data");
    return "/data/data/org.dxxjudges.clijudgepp/files/data";
#else
    return clijudge::platform::pathJoin(clijudge::platform::exeDir(), "data");
#endif
}

std::string configFilePath() {
#ifdef __ANDROID__
    const char* p = SDL_AndroidGetInternalStoragePath();
    if (p && p[0]) return clijudge::platform::pathJoin(p, "gui.json");
    return "gui.json";
#else
    return clijudge::gui::guiConfigPath();
#endif
}

// Android 物理像素密度 → 逻辑缩放（1dp ≈ dpi/160）
float detectUiScale() {
#ifdef __ANDROID__
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 0.0f) {
        float s = ddpi / 160.0f;
        if (s < 1.0f) s = 1.0f;
        if (s > 3.0f) s = 3.0f;
        return s;
    }
#endif
    return 1.0f;
}

std::vector<std::string> fontCandidates(const std::string& explicitPath) {
    std::vector<std::string> out;
    if (!explicitPath.empty()) out.push_back(explicitPath);
#ifdef _WIN32
    out.push_back("C:/Windows/Fonts/msyh.ttc");
    out.push_back("C:/Windows/Fonts/msyhbd.ttc");
    out.push_back("C:/Windows/Fonts/simhei.ttf");
    out.push_back("C:/Windows/Fonts/simsun.ttc");
#elif defined(__ANDROID__)
    out.push_back("/system/fonts/NotoSansCJK-Regular.ttc");
    out.push_back("/system/fonts/NotoSansCJKsc-Regular.otf");
    out.push_back("/system/fonts/NotoSansSC-Regular.otf");
    out.push_back("/system/fonts/DroidSansFallback.ttf");
    out.push_back("/system/fonts/NotoSansCJK-Regular.ttf");
#else
    out.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    out.push_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    out.push_back("/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf");
    out.push_back("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");
    out.push_back("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc");
    out.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
    return out;
}

ImFont* loadCjkFont(ImGuiIO& io, const std::string& explicitPath, float sizePx) {
    ImFontConfig fc;
    fc.OversampleH = 2;
    fc.OversampleV = 1;
    fc.PixelSnapH = true;

    // 常用字范围（2500 字）不含「渲」「辑」等字，用完整 CJK 范围避免缺字
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();

    for (const auto& p : fontCandidates(explicitPath)) {
        std::error_code ec;
        if (!fs::exists(p, ec) || fs::is_directory(p, ec)) continue;
        // .ttc 是字体集合，逐个尝试集合内序号
        for (int fontNo = 0; fontNo < 3; ++fontNo) {
            ImFontConfig cfg = fc;
            cfg.FontNo = fontNo;
            ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), sizePx, &cfg, ranges);
            if (f) return f;
            if (p.size() >= 4 && p.compare(p.size() - 4, 4, ".ttc") != 0) break;
        }
    }
    return io.Fonts->AddFontDefault();
}

void applyStyle(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.078f, 0.086f, 0.106f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.094f, 0.106f, 0.133f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.094f, 0.106f, 0.133f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.137f, 0.157f, 0.196f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.176f, 0.204f, 0.251f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.208f, 0.243f, 0.298f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.118f, 0.137f, 0.173f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.145f, 0.388f, 0.922f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.114f, 0.306f, 0.792f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.145f, 0.388f, 0.922f, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.145f, 0.388f, 0.922f, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.145f, 0.388f, 0.922f, 0.75f);
    c[ImGuiCol_Text] = ImVec4(0.871f, 0.894f, 0.933f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.518f, 0.553f, 0.616f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.259f, 0.298f, 0.361f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.078f, 0.086f, 0.106f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.204f, 0.235f, 0.290f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.118f, 0.137f, 0.173f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.145f, 0.388f, 0.922f, 0.7f);
    c[ImGuiCol_TabSelected] = ImVec4(0.145f, 0.388f, 0.922f, 0.55f);
    style.ScaleAllSizes(scale);
}

// 无界面自检：验证数据目录解析、数据层读写与 Markdown 解析
int runSelfTest(const std::string& dataDir) {
#ifdef _WIN32
    // GUI 子系统程序默认没有控制台；仅在输出句柄无效时才挂一个控制台
    // （若已被重定向到文件/管道，则保持原样，输出正常写入）
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }
#endif
    int failures = 0;
    printf("data_dir=%s\n", dataDir.c_str());

    clijudge::gui::DataService svc(dataDir);
    printf("counts: problems=%d articles=%d contests=%d submissions=%d\n",
           svc.countProblems(), svc.countArticles(), svc.countContests(), svc.countSubmissions());

    std::string plain = clijudge::gui::md::ToPlainText(
        "# 标题\n\n这是 **粗体** 与 `代码`，[链接](https://example.com)\n\n- 一\n- 二\n");
    printf("markdown_plain=%s\n", plain.c_str());
    if (plain.find("标题") == std::string::npos || plain.find("粗体") == std::string::npos) {
        printf("FAIL: markdown plain text mismatch\n");
        ++failures;
    }

    // 临时目录内做一次完整读写，避免污染真实数据
    fs::path tmp = fs::temp_directory_path() /
                   ("clijudge-gui-selftest-" + std::to_string(clijudge::platform::pid()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    {
        clijudge::gui::DataService t(tmp.string());
        int pid = t.createProblem("selftest problem");
        if (pid <= 0) {
            printf("FAIL: createProblem\n");
            ++failures;
        } else {
            if (t.countProblems() != 1) {
                printf("FAIL: countProblems after create\n");
                ++failures;
            }
            auto v = t.problemView(pid);
            if (!v.is_object() || !v.contains("problem")) {
                printf("FAIL: problemView\n");
                ++failures;
            }
            if (!t.deleteProblem(pid)) {
                printf("FAIL: deleteProblem\n");
                ++failures;
            }
        }
        int aid = t.createArticle("selftest article", "# hi\n");
        if (aid <= 0 || t.articleView(aid).is_null()) {
            printf("FAIL: article create/view\n");
            ++failures;
        }
        if (t.countProblems() != 0) {
            printf("FAIL: deleteProblem did not take effect\n");
            ++failures;
        }
    }
    fs::remove_all(tmp, ec);

    printf(failures == 0 ? "SELFTEST_OK\n" : "SELFTEST_FAILED\n");
    return failures == 0 ? 0 : 1;
}

void printUsage() {
    printf("CliJudge++ GUI\n"
           "Usage: %s [options]\n"
           "  --data-dir <path>  data directory (default: auto-detect, shares CLI data/)\n"
           "  --font <path>      TTF/TTC font file for CJK glyphs\n"
           "  --selftest         run headless data/markdown checks and exit\n"
           "  --help             show this help\n",
           kAndroid ? "clijudgepp" : "clijudgepp");
}

} // namespace

int main(int argc, char* argv[]) {
    clijudge::platform::initConsoleUtf8();

    std::string argDataDir;
    std::string argFont;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            printUsage();
            return 0;
        } else if (a == "--selftest") {
            selftest = true;
        } else if (a == "--data-dir" && i + 1 < argc) {
            argDataDir = argv[++i];
        } else if (a == "--font" && i + 1 < argc) {
            argFont = argv[++i];
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    GuiConfig cfg = clijudge::gui::loadGuiConfigAt(configFilePath());
    if (argFont.empty()) argFont = cfg.fontPath;

    std::string dataDir = clijudge::gui::resolveDataDir(cfg, platformDefaultDataDir());
    if (!argDataDir.empty()) {
        fs::path p(argDataDir);
        if (p.is_relative()) p = fs::path(clijudge::platform::exeDir()) / p;
        dataDir = p.lexically_normal().string();
    }
    std::error_code ec;
    fs::create_directories(dataDir, ec);
    clijudge::platform::migrateFlatLayout(dataDir);
    // 让 settings.h / lang.h / judge 使用同一数据目录
    SDL_setenv("CLIJUDGE_DATA_DIR", dataDir.c_str(), 1);

    if (selftest) {
        int rc = runSelfTest(dataDir);
        SDL_Quit();
        return rc;
    }

    float scale = detectUiScale();
    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#ifdef __ANDROID__
    flags = (SDL_WindowFlags)(flags | SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#endif
    int ww = cfg.windowWidth > 0 ? cfg.windowWidth : 1280;
    int wh = cfg.windowHeight > 0 ? cfg.windowHeight : 800;
    SDL_Window* window = SDL_CreateWindow(
        "CliJudge++", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!kAndroid && cfg.maximized) SDL_MaximizeWindow(window);

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    applyStyle(scale);

    float fontPx = (scale > 1.0f ? 17.0f : 18.0f) * scale;
    ImFont* font = loadCjkFont(io, argFont, fontPx);
    io.Fonts->Build();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    AppOptions opts;
    opts.dataDir = dataDir;
    opts.font = font;
    opts.uiScale = scale;
    opts.android = kAndroid;
    opts.judgeAvailable = !kAndroid;  // 移动端无本地编译工具链

    bool running = true;
    {
        UiApp app(opts);
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                    e.window.windowID == SDL_GetWindowID(window))
                    running = false;
            }

            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            app.Frame();
            ImGui::Render();

            SDL_SetRenderDrawColor(renderer, 20, 22, 27, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
