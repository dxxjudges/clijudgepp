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
  --lang <code>      显示语言（zh / en / langs 下的语言包名）
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
  "maximized": false,
  "lang": ""
}
```

配置优先级：命令行参数 > `gui.json` > 自动探测。配置损坏时回退默认值，不影响启动。
`lang` 为空时沿用 CLI `config.json` 的 `current_lang`，再回退到中文。

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
| 导入/导出 | 题目 CliJudge/NoldOJ JSON 或 zip（含 `testdata/`）、比赛 CDF；与 CLI 的 `problem export/import`、`contest export/import` 格式一致 |
| 显示语言 | 内置中文/English、本地 `.cjl` 语言包、在线获取语言包（与 CLI 同一语言源） |

评测结果状态与 clijudge 一致：`AC / WA / TLE / MLE / RE / CE / SE / SK / PE / OLE / PC / IV / ST / SR / IE`。

### 显示语言（语言包）

与 CLI 的 `displaylang` 使用**同一套语言包格式与存放位置**：`<dataDir>/langs/<code>.cjl`。
侧栏底部的「显示语言」下拉框可切换内置中文/English、本地语言包，或从在线语言源
下载新语言包；选择结果写入 `gui.json` 的 `lang`。

在线语言包放在仓库的 **`langs` 分支**（根目录 `*.cjl`），不占用主分支、新增语言无需发版：

- 列表：`https://api.github.com/repos/dxxjudges/clijudgepp/contents?ref=langs`
- 下载：`https://raw.githubusercontent.com/dxxjudges/clijudgepp/langs/<code>.cjl`

当前发布 `en.cjl`（English）与 `zh-CN.cjl`（简体中文）；新增语言只要往该分支提交
`<code>.cjl` 即可在应用内的在线列表中出现。

语言包结构（`strings.gui` 供 GUI 使用，其余节点留给 CLI）：

```json
{
  "meta": { "name": "ja", "display": "日本語", "version": "1.0.0" },
  "strings": {
    "gui": {
      "nav":       { "overview": "概要", "problems": "問題", "articles": "記事" },
      "common":    { "save": "保存", "cancel": "キャンセル", "delete": "削除" },
      "overview":  { "data_dir": "データディレクトリ" }
    }
  }
}
```

- 键为点路径（`nav.overview`），嵌在 `strings.gui` 下。
- 缺失的键按「语言包 → 内置英文 → 代码中的中文」回退，因此语言包可以只翻译一部分。
- 完整键表见 [src/i18n_en.inc](src/i18n_en.inc)；新增文案时在调用处写成 `i18n::t("key", "中文")`。
- 语言包文件名即语言代码，切换后立即生效，无需重启。

### Android 说明

移动端没有 g++ 等本地编译工具链与进程沙箱，**无法在设备上编译评测**；
界面会在提交入口明确提示，其余管理与浏览功能完整可用。

## CI

[.github/workflows/build.yml](.github/workflows/build.yml) 在 `push` / `pull_request` / `workflow_dispatch`
时构建以下产物：

- `windows-x64` / `windows-x86`（MSYS2 MinGW64 / MinGW32 + Ninja，单文件 exe）
- `windows-arm64`（llvm-mingw 交叉编译；产物需在 Windows on ARM 上运行，CI 只构建不跑测试）
- `linux-x64`（GCC + 系统 SDL2 依赖）
- `linux-arm64`（原生 ARM64 runner）
- `linux-x86`（32 位 Debian 容器内原生构建与测试）
- `android`（JDK 17 + NDK 26.1，`assembleDebug` APK）

构建前会把 `dxxjudges/clijudge` 检出到工作区 `clijudge/`，作为 include 源。
主项目版本在 `build.yml` 的 `CLIJUDGE_REF` 中固定为具体 commit，
上游 header 变更需要显式升级，避免 GUI 构建被动打挂。
推送 `v*` 标签时额外创建 Release 并上传各平台压缩包与 APK；
APK 的 `versionName` 取自 tag（`v1.2.3` → `1.2.3`），`versionCode` 取 CI 运行序号。

## 测试

```bash
ctest --test-dir build --output-on-failure   # Markdown 解析 + 比较模式表
./build/clijudgepp --selftest                # 数据目录探测与数据层只读冒烟
```

## 图标

`assets/icon.ico`（Windows exe）、`assets/icon.png`（Linux 分发）、
`src/icon_data.h`（运行时窗口图标）与 Android 各密度 `ic_launcher.png`
均由脚本生成，改设计后重新生成（需要 Pillow）：

```bash
python tools/make_icon.py
```

## 许可证

本项目以 [MIT License](LICENSE) 发布。

第三方组件：
- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [SDL2](https://github.com/libsdl-org/SDL) — zlib License（Android 端复制的 SDL Java 层见 `android/org.libsdl.LICENSE.txt`）

