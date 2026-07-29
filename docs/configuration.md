# Configuration Guide

> 中文版：[configuration.zh-CN.md](configuration.zh-CN.md)

Default file: [`Configuration/Configuration.toml`](../Configuration/Configuration.toml).

CppSentinel searches `search_path` (depth ≤ 3) for a file **exactly named**
`Configuration.toml`.

## Method table schema

```toml
[UniqueKey]
name = "DisplayName"          # optional; defaults to table key
enabled = true                # false → skipped
environment = "mingw64"       # mingw64 | clang64 | ucrt64 | clangarm64 | msys
command = """
  tool "{file_in}" > "{file_out}" 2>&1
"""
```

### Fields

| Field | Required | Description |
|-------|----------|-------------|
| `name` | no | Label used in logs and output file name |
| `enabled` | no (default false) | Whether the scheduler runs this method |
| `environment` | yes if enabled | MSYS2 environment / `MSYSTEM` |
| `command` | yes if enabled | Shell command template |

### Placeholders

- `{file_in}` — path passed as the second CLI argument (`check_path`)
- `{file_out}` — `report/<timestamp>/<SanitizedName>.out`

Paths are converted to MSYS form before substitution so bash redirections work
with Windows absolute paths.

## Built-in code-check methods

| Method | Default | Tool | Typical `{file_in}` |
|--------|---------|------|---------------------|
| `ClangTidy` | on | `clang-tidy` | single `.cpp` |
| `ClangFormat` | on | `clang-format --dry-run --Werror` | source/header |
| `CppSyntaxCheck` | on | `g++ -fsyntax-only` | `.cpp` |
| `ClangDiagnostics` | on | `clang++ -fsyntax-only` | `.cpp` |
| `Cppcheck` | on | `cppcheck` | file or directory |
| `Clazy` | off | `clazy-standalone` (Qt) | `.cpp` |
| `IncludeWhatYouUse` | off | `include-what-you-use` | `.cpp` |
| `Doxygen` | off | `doxygen` | `Doxyfile` |
| `LlvmCov` | off | `llvm-cov report` | coverage binary |

Enable only tools you have installed under the matching MSYS2 environment, e.g.:

```bash
# mingw64
pacman -S mingw-w64-x86_64-clang-tools-extra mingw-w64-x86_64-cppcheck

# clang64 (Clazy)
pacman -S mingw-w64-clang-x86_64-clazy
```

## Writing a good `command`

1. Quote paths: `"{file_in}"`, `"{file_out}"`.
2. Redirect both streams when you want a full log: `> "{file_out}" 2>&1`.
3. Prefer non-interactive flags (`--dry-run`, `-fsyntax-only`, …).
4. Keep one concern per method (format vs analysis vs docs).
5. If a tool needs `compile_commands.json`, add `-p /path/to/build` to that method.

### Example: project-wide clang-tidy with compilation database

```toml
[ClangTidyProject]
name = "ClangTidyProject"
enabled = true
environment = "mingw64"
command = """
clang-tidy -p "/c/path/to/build" "{file_in}" --export-fixes="{file_out}"
"""
```

## Tips

- Point `check_path` at a **single translation unit** for compiler/clang-tidy style tools.
- Point `check_path` at a **directory** for `cppcheck` recursive scans (adjust the command).
- Disable methods that need special inputs (`Doxygen`, `LlvmCov`) until artifacts exist.
