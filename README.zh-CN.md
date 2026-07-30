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

## 报告结构

```text
report/<run-id>/
  report.json              # 机器可读事实源
  summary.txt
  logs/<checker-id>.stdout.log
  logs/<checker-id>.stderr.log
  artifacts/<checker-id>/
```

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
