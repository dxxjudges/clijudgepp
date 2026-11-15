# CliJudge++（clijudgepp）

[CliJudge](../clijudge) 的原生图形界面版本：基于 **SDL2 + Dear ImGui** 的桌面应用，
并提供 **Android APK**（移动端仅支持管理与浏览）。

- **不复制数据层**：题目、文章、比赛、提交、评测全部直接复用 `clijudge/include` 中的
  `ProblemStore` / `ArticleStore` / `ContestStore` / `SubmitStore` / `judge` / `sandbox_runner`。
- **数据互通**：与 clijudge 命令行项目放在同一目录时，自动共享同一个 `data/` 目录与数据格式，
  并在修改类操作上持有与 CLI 相同的跨进程数据锁（`.clijudge.lock`）。
- **Markdown 渲染**：自写 C++ Markdown 解析器 + ImGui DrawList 自绘（标题/列表/代码块/表格/引用/链接），
  不依赖浏览器或第三方渲染库。
- **原生界面**：概览、题目、文章、比赛、提交记录，异步评测不阻塞 UI。

## 构建（桌面）

依赖：CMake ≥ 3.16、支持 C++17 的编译器、SDL2 源码（构建期自动获取）、Dear ImGui 子模块。

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

- SDL2 2.32.10 优先使用本地 `.deps/sdl2-src`；不存在时由 CMake FetchContent 按 SHA256 下载。
- Windows 使用 MinGW64 时静态链接（单文件 `clijudgepp.exe`）；Linux 动态链接，产物目录需同时
  带上 `libSDL2-2.0.so.0`（已配置 `$ORIGIN` rpath）。

### 与 clijudge 主项目同目录（推荐）

```
workspace/
├─ clijudge/          # 或 main/ —— 提供 include/ 与 data/
└─ clijudgepp/        # 本仓库
```

CMake 会自动依次查找以下 include 位置：

1. `-DCLIJUDGE_INCLUDE_DIR=<path>`
2. `<repo>/include`
3. `<repo>/clijudge/include`
4. `<repo>/../../main/include`
5. `<repo>/../clijudge/include`、`<repo>/../main/include`

## 构建（Android APK）

依赖：JDK 17、Android SDK（platform 34 / build-tools 34）、NDK 26.1、CMake 3.22。

```bash
cd android
./gradlew assembleDebug
# 产物: app/build/outputs/apk/debug/app-debug.apk
```

Gradle 通过 `externalNativeBuild` 直接复用仓库根目录的 `CMakeLists.txt`，
一次构建同时产出 `libSDL2.so` 与 `libmain.so`。

默认 ABI：`arm64-v8a`、`armeabi-v7a`、`x86_64`（可在 `android/app/build.gradle`
的 `abiFilters` 中裁剪以加快本地构建）。已在 Android 14 / arm64 真机验证：
全屏沉浸、概览统计、题目/文章/比赛/提交列表与 Markdown 渲染均正常。

## 运行

```
clijudgepp [options]
  --data-dir <path>  共享数据目录（默认自动探测）
  --font <path>      指定中文字体（TTF/TTC，默认自动挑选系统中文字体）
  --selftest         无界面自检（数据读写 + Markdown 解析）
  --help             显示帮助
```

### 配置文件

`gui.json`（放在可执行文件同目录；Android 放在应用私有目录，可参考 [gui.example.json](gui.example.json)）：

```json
{
  "data_dir": "",
  "font_path": "",
  "window_width": 1280,
  "window_height": 800,
  "maximized": false
}
```

配置优先级：命令行参数 > `gui.json` > 自动探测。配置损坏时回退默认值，不影响启动。

### 数据目录探测

1. 显式配置的 `data_dir`（相对路径基于可执行文件目录）；
2. 同目录树中的 clijudge 主项目源码（含 `include/platform.h` 等）→ 用其 `data/`；
3. 已存在的数据目录（含 `problems/`、`articles/`、`contests/`、`submissions/`、`langs/` 或 `config.json`）；
4. 平台默认：桌面 `<exe目录>/data/`，Android 应用私有存储 `<files>/data/`。

首次运行若目标数据目录不存在，会自动创建，并执行与 CLI 一致的旧版平铺布局迁移。

## 界面功能

| 模块 | 能力 |
| --- | --- |
| 概览 | 题目/文章/比赛/提交数量、数据目录、评测可用性 |
| 题目 | 列表、创建、编辑、删除；Markdown 题面；测试点增删改；选择源码提交评测 |
| 文章 | 列表、创建、查看、删除；Markdown 正文 |
| 比赛 | 创建/删除；查看比赛题目；比赛内提交；排行榜 |
| 提交记录 | 全局提交列表、单条详情（含子任务/测试点判定）、重判 |

评测结果状态与 clijudge 一致：`AC / WA / TLE / MLE / RE / CE / SE / SK / PE / OLE / PC / IV / ST / SR / IE`。

### Android 说明

移动端没有 g++ 等本地编译工具链与进程沙箱，**无法在设备上编译评测**；
界面会在提交入口明确提示，其余管理与浏览功能完整可用。

## CI

[.github/workflows/build.yml](.github/workflows/build.yml) 在 `push` / `pull_request` / `workflow_dispatch`
时构建四个产物：

- `windows-x64`（MSYS2 MinGW64 + Ninja，单文件 exe）
- `linux-x64`（GCC + 系统 SDL2 依赖）
- `linux-arm64`（原生 ARM64 runner）
- `android`（JDK 17 + NDK 26.1，`assembleDebug` APK）

构建前会把 `dxxjudges/clijudge` 检出到工作区 `clijudge/`，作为 include 源。
推送 `v*` 标签时额外创建 Release 并上传各平台压缩包与 APK。
