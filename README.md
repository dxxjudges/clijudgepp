# CliJudge++（clijudgepp）

[CliJudge](../clijudge) 的图形界面版本：单个可执行文件启动一个本地 Web 服务，浏览器打开即是完整的判题管理界面。

- **不复制数据层**：题目、文章、比赛、提交、评测全部直接复用 `clijudge/include` 中的
  `ProblemStore` / `ArticleStore` / `ContestStore` / `SubmitStore` / `judge` / `sandbox_runner`。
- **数据互通**：与 clijudge 命令行项目放在同一目录时，自动共享同一个 `data/` 目录与数据格式，
  并在修改类操作上持有与 CLI 相同的跨进程数据锁（`.clijudge.lock`）。
- **Markdown 渲染**：内置离线 Markdown 渲染器（题面、文章、比赛公告），无第三方前端依赖。
- **单文件分发**：HTML/CSS/JS 在构建期被内嵌进可执行文件，运行时不需要 `assets/` 目录。

## 构建

依赖：CMake ≥ 3.16、支持 C++17 的编译器、pthread（Linux）。

### 与 clijudge 主项目同目录（推荐）

```
workspace/
├─ clijudge/          # 或 main/ —— 提供 include/
└─ clijudgepp/        # 本仓库
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

CMake 会自动依次查找以下 include 位置：

1. `-DCLIJUDGE_INCLUDE_DIR=<path>`
2. `<repo>/include`
3. `<repo>/clijudge/include`
4. `<repo>/../../main/include`
5. `<repo>/../clijudge/include`、`<repo>/../main/include`

### 独立构建

```bash
cmake -S . -B build -DCLIJUDGE_INCLUDE_DIR=/path/to/clijudge/include
cmake --build build --parallel
```

Windows（MinGW64）：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

产物：`build/clijudgepp.exe`（Windows）或 `build/clijudgepp`（Linux）。

## 运行

```
clijudgepp [options]
  --host <addr>      监听地址（默认 127.0.0.1）
  --port <port>      监听端口（默认 8787，被占用时自动 +1 重试）
  --data-dir <path>  共享数据目录（默认自动探测）
  --no-browser       不自动打开浏览器
```

启动后控制台会打印实际监听的 URL，默认 <http://127.0.0.1:8787/>。

### 配置文件

`gui.json`（放在可执行文件同目录，可参考 [gui.example.json](gui.example.json)）：

```json
{
  "data_dir": "",
  "host": "127.0.0.1",
  "port": 8787,
  "open_browser": true
}
```

配置优先级：命令行参数 > `gui.json` > 自动探测。配置损坏时回退默认值，不影响启动。

### 数据目录探测

1. 显式配置的 `data_dir`（相对路径基于可执行文件目录）；
2. 同目录树中的 clijudge 主项目源码（含 `include/platform.h` 等）→ 用其 `data/`；
3. 已存在的数据目录（含 `problems/`、`articles/`、`contests/`、`submissions/`、`langs/` 或 `config.json`）；
4. `<exe目录>/data/`。

因此把本程序与 clijudge 放在同一目录（或把它丢进 clijudge 项目根目录）即可直接操作同一份数据。
首次运行若目标数据目录不存在，会自动创建，并执行与 CLI 一致的旧版平铺布局迁移。

## 界面功能

| 模块 | 能力 |
| --- | --- |
| 概览 | 题目/文章/比赛/提交数量、数据目录、语言配置状态 |
| 题目 | 列表、创建、编辑、删除；Markdown 题面；测试点增删改；粘贴或选择源码提交评测 |
| 文章 | 列表、创建、查看、删除；Markdown 正文 |
| 比赛 | 创建/删除；查看比赛题目；比赛内提交；排行榜；报告（HTML） |
| 提交记录 | 全局提交列表、单条详情（含子任务/测试点判定）、重判 |

评测结果状态与 clijudge 一致：`AC / WA / TLE / MLE / RE / CE / SE / SK / PE / OLE / PC / IV / ST / SR / IE`。

## HTTP API

所有接口为 `application/json`，仅供本地界面使用。

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/status` | 版本、数据目录、语言配置、各类计数 |
| GET / POST | `/api/problems` | 题目列表 / 创建 |
| GET / PUT / DELETE | `/api/problems/{id}` | 查看 / 编辑 / 删除题目 |
| GET / POST | `/api/problems/{id}/testcases` | 测试点列表 / 新增 |
| PUT / DELETE | `/api/problems/{id}/testcases/{tcId}` | 更新 / 删除测试点 |
| POST | `/api/problems/{id}/submit` | 提交并评测（`file_path` 或 `code`+`ext`） |
| GET / POST | `/api/articles` | 文章列表 / 创建 |
| GET / DELETE | `/api/articles/{id}` | 查看文章 / 删除 |
| GET / POST | `/api/contests` | 比赛列表 / 创建 |
| GET / DELETE | `/api/contests/{id}` | 查看 / 删除比赛 |
| GET | `/api/contests/{id}/leaderboard` | 排行榜 |
| GET | `/api/contests/{id}/report` | 比赛报告（HTML） |
| GET / POST | `/api/contests/{id}/problems/{idx}/submissions`、`/submit` | 比赛题目提交记录 / 提交 |
| GET | `/api/submissions`、`/api/submissions/{id}` | 提交列表 / 详情 |
| POST | `/api/submissions/{id}/rejudge` | 重判 |

## CI

[.github/workflows/build.yml](.github/workflows/build.yml) 在 `push` / `pull_request` / `workflow_dispatch`
时构建三个平台：

- `windows-x64`（MSYS2 MinGW64 + Ninja）
- `linux-x64`（GCC）
- `linux-arm64`（`aarch64-linux-gnu-g++` 交叉编译）

构建前会把 `dxxjudges/clijudge` 检出到工作区 `clijudge/`，作为 include 源。
推送 `v*` 标签时额外创建 Release 并上传各平台压缩包。
