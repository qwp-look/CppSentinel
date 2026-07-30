# 配置指南（v2）

> 英文版：[configuration.md](configuration.md)

默认配置：[`Configuration/Configuration.toml`](../Configuration/Configuration.toml)。

## CLI

必须显式指定配置文件与检查目标（无交互模式、无递归搜索配置）：

```bash
CppSentinel run --config path/to/Configuration.toml --target main.cpp
```

配置中的**相对路径**相对于**配置文件所在目录**解析。

生成 `compile_commands.json` 时请打开 CMake 的 `CMAKE_EXPORT_COMPILE_COMMANDS`
（本工程默认开启），并保证 `[project].compilation_database` 指向含该文件的构建目录
（例如 `../build/compile_commands.json`）。

## 完整 schema 示例

```toml
schema_version = 2

[project]
root = ".."
compilation_database = "../build/compile_commands.json"
report_dir = "../report"

[run]
max_parallel = 1
timeout_seconds = 120
fail_on = "warning"       # never|error|warning|style|information

[environment.native]
kind = "native"
inherit_parent = true

[environment.mingw64]
kind = "msys2"
root = "C:/msys64"
msystem = "MINGW64"       # MINGW64|CLANG64|UCRT64|CLANGARM64|MSYS
inherit_parent = true
# [environment.mingw64.env]
# KEY = "value"

[[checker]]
id = "clang-tidy"         # 全局唯一；映射日志/产物文件名
kind = "clang-tidy"
enabled = true
environment = "mingw64"
extra_args = []
# timeout_seconds = 180   # 可选；覆盖 run.timeout_seconds
```

允许的顶层键：`schema_version`、`project`、`run`、`environment`、`checker`（`[[checker]]`）。
任意遗留的 `[MethodName]` 表或 `command = """..."""` 会被拒绝。

## 默认值

| 字段 | 默认 |
|------|------|
| `checker.enabled` | `true` |
| `checker.environment` | `native` |
| `run.timeout_seconds` | `120` |
| `run.max_parallel` | `1`（仅支持 1） |
| `run.fail_on` | `warning` |
| `environment.*.inherit_parent` | `true` |

## 内建 kind

| kind | 作用 | 典型 `--target` | 依赖 compile_commands |
|------|------|-----------------|------------------------|
| `clang-tidy` | 静态分析；传入 `-p <build_dir>` | 单个 `.cpp` | **必需** |
| `clang-format` | 格式 dry-run（`--Werror`） | 源/头文件 | 否 |
| `compiler-diagnostics` | 从编译数据库还原参数并 `-fsyntax-only` | 单个 `.cpp` | **必需** |
| `cppcheck` | 独立静态分析；自动 `-I include` 与工程根 | 文件或目录 | 否 |

### 各 kind 可选字段

写入 checker 表的标量会进入内部 `options` 映射：

| 字段 | 适用 kind | 含义 |
|------|-----------|------|
| `executable` | 全部 | 覆盖默认可执行文件名 |
| `checks` | `clang-tidy` | `-checks=` 列表 |
| `header_filter` | `clang-tidy` | `-header-filter=` |
| `style` | `clang-format` | `--style=`（默认 `file`） |
| `compiler` | `compiler-diagnostics` | 无数据库条目时的回退编译器名 |
| `enable` | `cppcheck` | `--enable=` |
| `std` | `cppcheck` | `--std=` |
| `suppressions` | `cppcheck` | `--suppressions-list=` 路径 |
| `extra_args` | 全部 | 追加到工具 argv 的字符串数组 |
| `timeout_seconds` | 全部 | 覆盖全局超时 |

工具的默认可执行文件与基础参数在代码中（`BuiltinCheckers`）；配置只描述启用状态、环境、选项与附加参数。

## 校验规则

- `schema_version` 必须为整数 `2`
- 每个 `checker.id` 全局唯一（用于日志/产物名，需可净化为文件名）
- `environment` 必须引用已定义的 profile（内建始终提供 `native`）
- 未知顶层键 / 未知字段 → 错误（带字段路径；TOML 语法错误带行列）
- 拒绝 `command` shell 模板
- Linux 上 `kind = "msys2"` 的环境定义 → 配置错误
- `[[checker]]` 执行顺序 = 书写顺序（不是表名排序）

加载失败时 CLI 打印全部 `ConfigDiagnostic`（路径、消息、修复提示），进程退出码 `1`。

## fail_on

| 值 | 失败条件 |
|----|----------|
| `never` | 仅硬失败（启动/超时/工具无法解析退出等） |
| `error` | 诊断 severity ≥ error |
| `warning` | ≥ warning（默认） |
| `style` | ≥ style |
| `information` | 任意诊断 |

下列 **CheckerStatus** 始终使运行失败，与 `fail_on` 无关：
`StartError`、`ToolError`、`TimedOut`、`ConfigError`。

工具正常跑完但产生诊断时，状态可为 `Succeeded`，由诊断严重级别与 `fail_on` 决定退出码。
格式问题应体现为 `style` 诊断（见 `clang-format`），而不是只留下“非零退出码”这一事实。

## 编写建议

1. 先配置好 `compilation_database`，再启用 `clang-tidy` / `compiler-diagnostics`。  
2. Windows 上需要 MinGW 工具链时使用 `environment = "mingw64"`（或自建 msys2 profile）。  
3. 附加 flag 放进 `extra_args`，不要恢复多行 shell `command`。  
4. 一个 `[[checker]]` 只做一类事（格式 / 分析 / 编译诊断分开）。  
5. 需要检查本仓库时，确保构建目录中的 `compile_commands.json` 含 `main.cpp` 等目标条目。
