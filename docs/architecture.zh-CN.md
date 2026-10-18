# 架构说明（v2）

> 英文版：[architecture.md](architecture.md)

CppSentinel v2 是**可配置的 C++ 代码审计编排器**。它本身不做静态分析，而是：

1. 加载并校验 v2 TOML 配置  
2. 通过注册表创建内建 Checker  
3. 经 Native 或 MSYS2 进程层执行工具  
4. 将诊断汇总为结构化 `RunReport` 并写出报告目录  

## 流水线

```text
CLI: run --config --target
        │
        ▼
 ConfigLoader  ──► AppConfig（失败时返回 ConfigDiagnostic 列表）
        │
        ▼
 Engine
   创建独占 report/<run-id>/
   按 [[checker]] 声明顺序：
     - disabled → Skipped
     - CheckerRegistry::create(kind)
     - 选择 ProcessRunner（native / msys2，测试可注入 Fake）
     - Checker::run(CheckerContext)
         → 构造 ProcessRequest（可执行文件 + argv）
         → ProcessRunner::run
         → 解析 stdout/stderr 日志 → Diagnostic[]
     - 追加 CheckerResult
        │
        ▼
 buildUnifiedReport  ──► UnifiedReport（路径/规则/严重度归一 + 去重 + 稳定 id + 汇总）
        │
        ▼
 ReportWriter  ──► report.json / report.raw.json / report.sarif / report.md
                   + summary.txt + logs/ + artifacts/（均原子发布）
        │
        ▼
 Console summary + exitCodeFor(fail_on)
```

## 报告分层

```text
CheckerResult[].Diagnostic[]   原始层：工具原样输出，逐字段可追溯（report.raw.json）
        │  Findings.cpp 归一化
        ▼
UnifiedReport.findings[]        统一层：一条 finding = 一个已归位的问题（report.json）
        │  Reporter
        ▼
JSON / SARIF / Markdown / 控制台   表现层：只读统一层，不再各自解析工具输出
```

归一化规则（`src/report/Findings.cpp`）：

| 步骤 | 处理 |
|------|------|
| 路径 | 绝对路径按项目根转相对、`/` 分隔；根外路径保持绝对并记 note |
| 位置 | `line/column == 0` 视为无位置；`file == "nofile"` 视为无文件 |
| 严重度 | 工具 token → `error/warning/style/information/unknown`，去重时取最严重 |
| 类别 | 规则前缀映射到 `build/defect/security/performance/maintainability/style` |
| 去重 | 指纹 = 路径 + 行 + 列 + 规则 + 归一化消息；合并 `occurrences` 与来源工具 |
| note | `role == Note` 的诊断挂到同文件的上一条主诊断，不单列 |
| 元信息 | 无位置且 `information` 的工具自述信息移入 `notes.tool_notices` |
| id | `CS-<fnv1a64>`，跨运行稳定，可用于基线比对 |

## 源码布局

```text
main.cpp                         CLI 入口
include/cppsentinel/
  config/                        AppConfig、ConfigLoader
  core/                          Engine、CheckerRegistry
  checker/                       Checker 接口与内建实现辅助头
  process/                       ProcessRequest、Runner、路径/环境
  report/                        Diagnostic、RunReport、Reporter
  util/                          文件 IO、时间戳
src/                             与上述模块对应的实现
cmake/BoostProcess.cmake         内嵌 Boost.Process 源列表
Configuration/Configuration.toml 默认 v2 配置
tests/unit|integration           CTest
```

## 模块职责

| 模块 | 职责 |
|------|------|
| `main.cpp` | 仅 CLI：解析 `run --config --target`、加载配置、调用 Engine、打印汇总 |
| `config` | TOML 解析、schema 校验、相对路径解析 → 不可变 `AppConfig` |
| `core` | `Engine` 编排；`CheckerRegistry` 按 `kind` 创建 Checker |
| `checker` | 内建检查器；`CompileCommands` / `DiagnosticParser` |
| `process` | `ProcessRequest/Result`；`NativeProcessRunner`、`Msys2ProcessRunner`、`FakeProcessRunner` |
| `report` | `RunReport`（原始层）→ `UnifiedReport`（`Findings` 统一层）→ JSON / SARIF / Markdown / 控制台；独占报告目录与日志路径 |

**边界约定：**

- Checker **不**打印控制台汇总、**不**创建报告根目录、**不**计算进程退出码。  
- Engine **不**解析 TOML、**不**解析工具输出、**不**拼 shell 命令。  
- 路径在 Core/Checker 中保持 `std::filesystem::path`；转 MSYS POSIX 仅发生在 MSYS2 Runner 边界。

## 进程模型

- **Native**（Linux / Windows）：直接 `executable + arguments`，stdout/stderr 写入日志文件，支持超时终止。  
- **MSYS2**（仅 Windows）：

```text
<MSYS2>/usr/bin/bash.exe -l -c 'exec "$@"' cppsentinel-tool <exe> <arg...>
```

不经过 `cmd.exe` / `msys2_shell.cmd`，避免嵌套引号。参数作为独立 argv，不做文本拼接。

参数按**原生 Windows 路径**原样传入，并设置 `MSYS2_ARG_CONV_EXCL=*` 关闭 MSYS2 运行时的
argv 路径重写。否则 `C:/a/b` 会被二次转换成 `C:/c/a/b`，`--export-fixes=C:/...`
这类带路径的 flag 也会被误判为路径。

子进程环境 = 可选父环境拷贝 + profile/`ProcessRequest` 覆盖；MSYS2 强制：

| 变量 | 值 |
|------|-----|
| `MSYSTEM` | 如 `MINGW64` / `CLANG64` |
| `CHERE_INVOKING` | `1` |
| `MSYS2_PATH_TYPE` | `inherit` |

## 报告与失败策略

每次运行生成：

```text
report/<run-id>/
  report.json          # 机器可读事实源
  summary.txt          # 与控制台同源摘要
  logs/<id>.stdout.log
  logs/<id>.stderr.log
  artifacts/<id>/      # 如 clang-tidy fixes、clang-format XML
```

`run-id` 为本地时间戳（含毫秒）；冲突时追加后缀，**从不复用**已存在目录。

### CheckerStatus

| 状态 | 含义 | 对退出码 |
|------|------|----------|
| `Succeeded` | 工具跑完（可有诊断） | 由 `fail_on` + 诊断严重级别决定 |
| `Skipped` | `enabled = false` | 不影响失败 |
| `TimedOut` / `StartError` / `ToolError` / `ConfigError` | 硬失败 | 始终导致非 0 |

### 进程退出码

| 码 | 含义 |
|----|------|
| 0 | 在 `[run].fail_on` 下全部通过 |
| 1 | 启动级失败（CLI/配置/报告目录/报告写入等） |
| 2 | 至少一个 checker 未通过策略 |

## 扩展方式

新增 Checker **无需修改** `Engine`：

1. 实现 `Checker`（`kind()` + `run(const CheckerContext&)`）  
2. 在 `registerBuiltinCheckers()` 注册工厂  
3. 在配置中使用新的 `kind`，并补充单元/集成测试  

当前为**编译期注册**；不做动态 DLL 插件，不做并行执行（`max_parallel` 固定为 1）。
