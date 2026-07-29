# CppSentinel

可配置的 **C++ 代码审计编排器**。它从 `Configuration.toml` 加载检查项，在 MSYS2
环境（`mingw64`、`clang64` 等）中执行，并把每个工具的输出写到
`report/<时间戳>/`。

> 英文版：[README.md](README.md)

## 功能特点

- 用 TOML 声明工具列表（clang-tidy、clang-format、cppcheck、g++/clang 语法检查等）
- 稳健的 MSYS2 启动方式：`bash -l -c` + `MSYSTEM`（不再嵌套 `cmd` / `msys2_shell` 引号）
- 带时间戳、可写校验的报告目录；每个 method 单独输出文件
- 通过 **bcp** 内嵌 Boost.Process 头文件与源码（构建时不依赖系统 Boost 安装）

## 依赖

- CMake 3.16+
- C++20 编译器（MinGW-w64 / MSVC / clang-cl）
- MSYS2，以及配置文件中用到的对应工具
- 构建本身不需要系统 Boost，见 `third_party/boost`

## 构建

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

在 MSYS2 MinGW 下：

```bash
export PATH=/mingw64/bin:$PATH
cmake -S . -B build -G Ninja
cmake --build build
```

## 用法

```bash
CppSentinel <search_path> <check_path>
```

| 参数 | 含义 |
|------|------|
| `search_path` | 在此目录树中查找 `Configuration.toml`（深度 ≤ 3） |
| `check_path` | 替换到命令模板里的 `{file_in}` |

交互模式（无参数，提示输入两个路径）：

```bash
CppSentinel
```

### 环境变量

| 变量 | 含义 |
|------|------|
| `MSYS2_ROOT` | MSYS2 安装根目录（需包含 `usr/bin/bash.exe`） |
| `MSYS2_PATH` | 备选根目录 |

未设置时会尝试常见路径，例如 `C:\msys64`。

### 示例

```bash
./build/CppSentinel.exe ./Configuration ./main.cpp
```

## 配置

- [`Configuration/Configuration.toml`](Configuration/Configuration.toml) — 默认检查项
- [`docs/configuration.zh-CN.md`](docs/configuration.zh-CN.md) — 字段说明与编写建议

占位符：

| 占位符 | 替换为 |
|--------|--------|
| `{file_in}` | `check_path`（转成 MSYS POSIX 路径，如 `/c/proj/main.cpp`） |
| `{file_out}` | `report/<时间戳>/<MethodName>.out` |

## 目录结构

```text
main.cpp                 仅负责编排
include/                 对外头文件
src/                     实现
Configuration/           默认 Configuration.toml
docs/                    架构 / 配置 / Boost 内嵌说明
third_party/toml.hpp     toml++ 单头文件
third_party/boost/       bcp 抽出的 Boost.Process 及依赖
scripts/vendor_boost.ps1 用 bcp 刷新内嵌 Boost
report/                  生成报告（内容已 gitignore）
```

## 内嵌 Boost（bcp）

头文件与 Process 源码位于 [`third_party/boost`](third_party/boost)。
CMake 会编译 `libs/process/src/*.cpp`，并定义
`BOOST_PROCESS_USE_STD_FS`（运行时使用 `std::filesystem`）。

升级 Boost 后刷新：

```powershell
.\scripts\vendor_boost.ps1 -BoostSource "C:\path\to\boost_1_91_0"
```

详见 [`docs/boost_vendor.zh-CN.md`](docs/boost_vendor.zh-CN.md)。

## 执行模型

```text
bash.exe -l -c "<command>"
  MSYSTEM=MINGW64|CLANG64|...
  CHERE_INVOKING=1
  MSYS2_PATH_TYPE=inherit
```

架构说明：[`docs/architecture.zh-CN.md`](docs/architecture.zh-CN.md)。

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 所有已启用 method 均启动成功且退出码为 0 |
| 1 | 启动/配置/报告目录失败 |
| 2 | 至少一个 method 启动失败或返回非 0 |

`enabled = false` 的项会被跳过，不影响退出码。

## 文档索引

| 文档 | 说明 |
|------|------|
| [architecture.zh-CN.md](docs/architecture.zh-CN.md) | 架构与流水线 |
| [configuration.zh-CN.md](docs/configuration.zh-CN.md) | 配置与命令编写 |
| [boost_vendor.zh-CN.md](docs/boost_vendor.zh-CN.md) | bcp 内嵌 Boost |

## 许可证

项目代码见 [LICENSE](LICENSE)。  
内嵌 Boost 见 [Boost Software License 1.0](third_party/boost/LICENSE_1_0.txt)。
