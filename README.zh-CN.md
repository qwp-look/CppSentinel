# CppSentinel

可配置的 **C++ 代码审计编排器**（v2）。从 TOML 加载检查器，经统一进程层
（Windows MSYS2 或 native）执行，并在 `report/<run-id>/` 写入结构化报告。

> 英文版：[README.md](README.md)

## 功能特点

- 声明式 `[[checker]]` 列表（clang-tidy、clang-format、编译器诊断、cppcheck）
- 默认配置不再使用原始 shell 命令模板
- 统一 `ProcessRequest` 执行（Native + MSYS2；参数为独立 argv）
- 可机器读取的 `report.json`、原始日志与控制台汇总
- 通过 bcp 内嵌 Boost.Process（构建不依赖系统 Boost）
- MinGW 下默认静态链接运行时，便于在未配置 MinGW `PATH` 的 PowerShell 中启动

## 依赖

- CMake 3.16+
- C++20 编译器（MinGW-w64 / MSVC / clang / g++）
- 已启用检查器对应的工具（Windows 上通常来自 MSYS2）
- 构建本身不需要系统 Boost，见 `third_party/boost`

## 构建

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake 默认开启 `CMAKE_EXPORT_COMPILE_COMMANDS`，会在构建目录生成
`compile_commands.json`，供 `clang-tidy` / `compiler-diagnostics` 使用。

可选：`-DCPPSENTINEL_BUILD_TESTS=OFF` 关闭测试目标。

## 用法

```bash
CppSentinel run --config <Configuration.toml> --target <file-or-dir>
```

| 参数 | 含义 |
|------|------|
| `--config` | v2 配置文件路径（必须显式指定） |
| `--target` | 检查目标（源文件或目录，语义由各 checker 解释） |
| `--baseline` | 基线文件；只把不在基线里的 finding 视为新问题 |
| `--update-baseline` | 用本次结果重写基线（退出码只看工具是否正常）|
| `--jobs` | 并行运行的 checker 数量（1..16） |
| `--cache` | 项目未变化时复用上次结果 |

子命令：`run`（审计）、`doctor`（环境自检）、`init`（生成配置模板）。

示例：

```bash
./build/CppSentinel.exe run --config ./Configuration/Configuration.toml --target ./main.cpp
```

### 环境变量

| 变量 | 含义 |
|------|------|
| `MSYS2_ROOT` | MSYS2 安装根目录（需包含 `usr/bin/bash.exe`） |
| `MSYS2_PATH` | 备选根目录 |

未设置时，MSYS2 Runner 会尝试常见路径（如 `C:\msys64`），或使用配置里
`[environment.*.root]`。

## 配置

- 默认：[`Configuration/Configuration.toml`](Configuration/Configuration.toml)
- 说明：[`docs/configuration.zh-CN.md`](docs/configuration.zh-CN.md)

要点：

```toml
schema_version = 2

[project]
root = ".."
compilation_database = "../build/compile_commands.json"
report_dir = "../report"

[[checker]]
id = "clang-tidy"
kind = "clang-tidy"
environment = "mingw64"
```

相对路径相对于**配置文件所在目录**解析。若构建目录不是 `build`，请修改
`compilation_database`。

## 目标展开

`--target` 可以是文件或目录。目录在任何工具启动前**只展开一次**，保证
clang-tidy、clang-format、compiler-diagnostics、cppcheck 面对完全相同的文件集：

- 只收 C/C++ 扩展名（`.c .cc .cpp .cxx .h .hh .hpp .hxx .ipp .inl` 等）
- 跳过 `build`、`build-*`、`node_modules` 与点开头的目录
- 不可读子目录会中止遍历，并在报告 `notes` 中提示
- `[filter].ignore_paths` 的 glob 同时用于排除分析目标

随后每个 checker 按最多 40 个文件一批调用工具（clang-format 逐文件调用，便于定位失败文件），
`[run].max_parallel`（1..16）决定同时运行几个 checker。报告顺序始终按 `[[checker]]`
声明顺序，保证可复现。

## 缓存

`--cache`（或 `[run].cache = true`）在项目未变化时复用上次的 checker 结果。缓存键包含
checker 选项、工具版本，以及项目根下所有 C/C++ 文件的指纹（路径、大小、mtime），
因此升级工具或改动源码都会失效。只有诊断结果被缓存——报告里会标注 `(cached)`，
不会假装上次的日志还存在。

本仓库实测（`./src/util`，4 个 checker）：冷启动 4.4 秒，命中缓存 260 毫秒。

## CI

`.github/workflows/ci.yml` 在构建后运行 `self-audit`：先 `doctor` 自检，再用
`Configuration/Configuration.ci.toml`（native 环境、`fail_on = "error"`、
`ignore_outside_project = true` 避免标准库头文件告警阻断构建）跑全量审计，
最后上传 `report.md` / `report.sarif` / `report.json`。若要按"回归"而非"绝对状态"
卡门，提交基线并启用 `fail_on_new`。

## 过滤与基线

不是每条问题都值得处理。过滤器会显式丢弃并记录原因，而不是悄悄隐藏：

```toml
[filter]
ignore_rules = ["modernize-*"]        # 按规则 id 匹配（glob）
ignore_paths = ["third_party/**"]     # 按项目相对路径匹配
ignore_messages = ["is too short"]    # 消息子串，忽略大小写
min_severity = "warning"              # 丢弃 style/information
ignore_outside_project = true         # 丢弃系统/工具链头文件
inline_suppressions = true
```

源码内抑制：

```cpp
// cppsentinel-disable-next-line readability-identifier-length
int x;
// cppsentinel-disable cert-err33-c        <- 本行之后整个文件
```

被抑制的 finding 不会消失：它们带原因（`config:ignore_rules`、`inline:disable` 等）
记录在 `report.json` 的 `suppressed` 中，并在汇总里计数。

基线把存量问题变成"已接受集合"：

```bash
CppSentinel run --config ... --target src --update-baseline
CppSentinel run --config ... --target src --baseline Configuration/baseline.json
```

启用基线后只有 `new` 会失败，消失的条目记为 `fixed`。匹配先按稳定 id，
再按 文件+规则+消息 兜底，因此仅仅行号变动不会变成新问题。

## 报告结构

单次运行产出统一报告，而不是把各工具输出拼接在一起：

```text
report/<run-id>/
  report.json              # 统一 findings（schema 3）：机器可读事实源
  report.raw.json          # 各 checker 原始诊断（schema 2）：可追溯
  report.sarif             # SARIF 2.1.0：GitHub code scanning / IDE 可直接消费
  report.md                # 人读报告：概览 + 工具执行 + 按文件明细
  summary.txt              # 控制台摘要（与 stdout 同源）
  logs/<checker-id>.stdout.log
  logs/<checker-id>.stderr.log
  artifacts/<checker-id>/
```

统一层做的规范化：

- **路径归一**：绝对路径转成相对项目根的 `/` 分隔路径，跨工具同文件可直接对齐
- **规则与类别归一**：`bugprone-*` → defect、`cert-*` → security、`performance-*` → performance 等
- **去重合并**：同一位置同一规则的重复诊断（典型是头文件在多个 TU 里各报一次）合并为一条，记录 `occurrences` 与来源工具
- **稳定指纹**：每条 finding 带 `id`（`CS-<hash>`），跨运行不变，可用于基线比对
- **注释归位**：clang/gcc 的 `note:` 行挂到所属诊断下，不再单列为问题
- **元信息隔离**：`checkersReport`、`unmatchedSuppression` 等工具自述信息移出问题列表，进入 `notes`
- **可信度提示**：超时/启动失败/配置错误的 checker 会在 `notes` 中显式标注"结果可能不完整"
- **版本溯源**：每个工具的 `--version` 写入 `metadata.tool_versions`
- **代码片段**：每条 finding 带上出错行原文（`snippet`）
- **自动修复建议**：clang-tidy 的 replacement 转成 `fixes[]`，SARIF 也带 `fixes`

`report.json` 顶层为 `summary`（按严重度/类别/文件/规则的汇总）+ `findings`（排序后的明细）。

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 在 `[run].fail_on` 策略下全部通过 |
| 1 | 启动 / 配置 / 报告目录或写入失败 |
| 2 | 至少一个检查器未通过策略（含硬失败或诊断触发 fail_on） |

`enabled = false` 的项计为跳过，不单独导致失败。

## 目录结构

```text
main.cpp                      CLI
include/cppsentinel/          公共头文件（config/core/checker/process/report）
src/                          实现
cmake/BoostProcess.cmake      内嵌 Boost.Process
Configuration/                默认 Configuration.toml
tests/                        单元与集成测试（CTest）
docs/                         架构 / 配置 / Boost 说明
scripts/vendor_boost.ps1      bcp 刷新脚本
third_party/toml.hpp          toml++
third_party/boost/            bcp 抽出的 Boost.Process
report/                       运行产物（内容已 gitignore）
```

## 文档索引

| 文档 | 说明 |
|------|------|
| [docs/README.md](docs/README.md) | 文档总索引 |
| [architecture.zh-CN.md](docs/architecture.zh-CN.md) | 架构与流水线 |
| [configuration.zh-CN.md](docs/configuration.zh-CN.md) | 配置与编写建议 |
| [boost_vendor.zh-CN.md](docs/boost_vendor.zh-CN.md) | bcp 内嵌 Boost |

## 许可证

项目代码见 [LICENSE](LICENSE)。  
内嵌 Boost 见 [Boost Software License 1.0](third_party/boost/LICENSE_1_0.txt)。
