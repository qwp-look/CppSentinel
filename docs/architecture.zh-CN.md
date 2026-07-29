# 架构说明

> 英文版：[architecture.md](architecture.md)

CppSentinel 是一个**可配置的 C++ 代码审计编排器**。它本身不做静态分析，而是：

1. 从 TOML 加载工具命令  
2. 在 MSYS2 环境中执行  
3. 把输出收集到 `report/`

## 流水线

```text
命令行 / 交互输入
        │
        ▼
 FileFinder  ── search_path ──► Configuration.toml
        │
        ▼
 TomlParser  ──► vector<Method>
        │
        ▼
 ReportDir   ──► report/<年_月_日_时_分_秒_毫秒>/
        │
        ▼
 Scheduler
   对每个 Method：
     - disabled 则跳过
     - CommandBuilder 替换 {file_in}/{file_out}
     - Msys2Runner 在对应 MSYSTEM 下执行 bash -l -c
     - 记录退出码
        │
        ▼
 汇总 + 进程退出码
```

## 模块职责

| 模块 | 职责 |
|------|------|
| `main.cpp` | 参数解析与高层编排 |
| `FileFinder` | BFS 查找 `Configuration.toml` |
| `TomlParser` | 解析 method 表为 `Method` |
| `ReportDir` | 创建可写的时间戳报告目录 |
| `CommandBuilder` | 占位符展开 + Windows→MSYS 路径转换 |
| `Msys2Runner` | 定位 MSYS2、构造子进程环境、执行 `bash -l -c` |
| `Scheduler` | 依次运行 method 并汇总成败 |

## MSYS2 执行模型

**不要**再走 `cmd.exe` + `msys2_shell.cmd`（多层引号极易出错）。

正确方式：

```text
<MSYS2_ROOT>/usr/bin/bash.exe -l -c "<command>"
```

子进程环境 = 父进程环境拷贝，再覆盖：

| 变量 | 值 |
|------|-----|
| `MSYSTEM` | `MINGW64` / `CLANG64` / … |
| `CHERE_INVOKING` | `1` |
| `MSYS2_PATH_TYPE` | `inherit` |

TOML 中 `environment` 映射：

| TOML | `MSYSTEM` |
|------|-----------|
| `mingw64` | `MINGW64` |
| `clang64` | `CLANG64` |
| `ucrt64` | `UCRT64` |
| `clangarm64` | `CLANGARM64` |
| `msys` | `MSYS` |

## 占位符

| 占位符 | 含义 |
|--------|------|
| `{file_in}` | CLI 的 `check_path`，转为 MSYS POSIX（`C:\a\b` → `/c/a/b`） |
| `{file_out}` | 报告目录下该 method 的输出文件 |

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 所有已启用 method 启动成功且返回 0 |
| 1 | 启动失败（参数、配置、报告目录等） |
| 2 | 至少一个 method 启动失败或返回非 0 |

禁用项不计入失败。
