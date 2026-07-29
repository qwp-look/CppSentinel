# 配置指南

> 英文版：[configuration.md](configuration.md)

默认配置：[`Configuration/Configuration.toml`](../Configuration/Configuration.toml)。

CppSentinel 会在 `search_path` 下（深度 ≤ 3）查找**文件名恰好为**
`Configuration.toml` 的文件。

## Method 表结构

```toml
[UniqueKey]
name = "DisplayName"          # 可选；默认用表名
enabled = true                # false 则跳过
environment = "mingw64"       # mingw64 | clang64 | ucrt64 | clangarm64 | msys
command = """
  tool "{file_in}" > "{file_out}" 2>&1
"""
```

### 字段

| 字段 | 是否必需 | 说明 |
|------|----------|------|
| `name` | 否 | 日志显示名与输出文件名 |
| `enabled` | 否（默认 false） | 调度器是否执行 |
| `environment` | 启用时建议必填 | MSYS2 环境 / `MSYSTEM` |
| `command` | 启用时建议必填 | Shell 命令模板 |

### 占位符

- `{file_in}` — 第二个 CLI 参数 `check_path`
- `{file_out}` — `report/<时间戳>/<净化后的名称>.out`

替换前会把路径转成 MSYS 形式，这样 bash 重定向才能正确处理 Windows 绝对路径。

## 内置代码检查项

| Method | 默认 | 工具 | 典型 `{file_in}` |
|--------|------|------|------------------|
| `ClangTidy` | 开 | `clang-tidy` | 单个 `.cpp` |
| `ClangFormat` | 开 | `clang-format --dry-run --Werror` | 源/头文件 |
| `CppSyntaxCheck` | 开 | `g++ -fsyntax-only` | `.cpp` |
| `ClangDiagnostics` | 开 | `clang++ -fsyntax-only` | `.cpp` |
| `Cppcheck` | 开 | `cppcheck` | 文件或目录 |
| `Clazy` | 关 | `clazy-standalone`（偏 Qt） | `.cpp` |
| `IncludeWhatYouUse` | 关 | `include-what-you-use` | `.cpp` |
| `Doxygen` | 关 | `doxygen` | `Doxyfile` |
| `LlvmCov` | 关 | `llvm-cov report` | 覆盖率产物 |

只启用你在对应 MSYS2 环境里已安装的工具，例如：

```bash
# mingw64
pacman -S mingw-w64-x86_64-clang-tools-extra mingw-w64-x86_64-cppcheck

# clang64（Clazy）
pacman -S mingw-w64-clang-x86_64-clazy
```

## 如何写好 `command`

1. 路径加引号：`"{file_in}"`、`"{file_out}"`。
2. 需要完整日志时重定向两边：`> "{file_out}" 2>&1`。
3. 优先非交互参数（`--dry-run`、`-fsyntax-only` 等）。
4. 一个 method 只做一件事（格式 / 分析 / 文档分开）。
5. 若工具需要 `compile_commands.json`，在该 method 中加 `-p /path/to/build`。

### 示例：带编译数据库的项目级 clang-tidy

```toml
[ClangTidyProject]
name = "ClangTidyProject"
enabled = true
environment = "mingw64"
command = """
clang-tidy -p "/c/path/to/build" "{file_in}" --export-fixes="{file_out}"
"""
```

## 使用提示

- 编译器 / clang-tidy 类工具：`check_path` 指向**单个翻译单元**更合适。
- `cppcheck` 可扫目录：把 `check_path` 指到目录，并相应改命令。
- `Doxygen`、`LlvmCov` 需要特殊输入，没有产物前请保持 `enabled = false`。
