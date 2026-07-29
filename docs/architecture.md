# Architecture

> 中文版：[architecture.zh-CN.md](architecture.zh-CN.md)

CppSentinel is a **configurable C++ code-audit orchestrator**. It does not implement
static analysis itself. It loads tool commands from TOML, runs them inside MSYS2
environments, and collects outputs under `report/`.

## Pipeline

```text
CLI / interactive input
        │
        ▼
 FileFinder  ── search_path ──► Configuration.toml
        │
        ▼
 TomlParser  ──► vector<Method>
        │
        ▼
 ReportDir   ──► report/<yyyy_mm_dd_HH_MM_SS_mmm>/
        │
        ▼
 Scheduler
   for each Method:
     - skip if disabled
     - CommandBuilder replaces {file_in}/{file_out}
     - Msys2Runner launches bash -l -c in MSYSTEM env
     - record exit code
        │
        ▼
 Summary + process exit code
```

## Modules

| Module | Responsibility |
|--------|----------------|
| `main.cpp` | Argument parsing, high-level orchestration only |
| `FileFinder` | BFS locate `Configuration.toml` |
| `TomlParser` | Parse method tables into `Method` |
| `ReportDir` | Create writable timestamped report directories |
| `CommandBuilder` | Placeholder expansion + Windows→MSYS path conversion |
| `Msys2Runner` | Discover MSYS2, build child env, run `bash -l -c` |
| `Scheduler` | Run all methods, aggregate success/failure |

## MSYS2 execution model

Tools are **not** invoked through `cmd.exe` + `msys2_shell.cmd` (nested quoting is fragile).

Instead:

```text
<MSYS2_ROOT>/usr/bin/bash.exe -l -c "<command>"
```

Child environment (copied from the parent, then overridden):

| Variable | Value |
|----------|-------|
| `MSYSTEM` | `MINGW64` / `CLANG64` / … |
| `CHERE_INVOKING` | `1` |
| `MSYS2_PATH_TYPE` | `inherit` |

`environment` in TOML maps as:

| TOML | `MSYSTEM` |
|------|-----------|
| `mingw64` | `MINGW64` |
| `clang64` | `CLANG64` |
| `ucrt64` | `UCRT64` |
| `clangarm64` | `CLANGARM64` |
| `msys` | `MSYS` |

## Placeholders

| Token | Meaning |
|-------|---------|
| `{file_in}` | CLI `check_path`, converted to MSYS POSIX (`C:\a\b` → `/c/a/b`) |
| `{file_out}` | Per-method file under the report directory |

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Every enabled method launched and returned 0 |
| 1 | Startup failure (args, config, report dir, …) |
| 2 | At least one method failed to launch or returned non-zero |

Disabled methods are skipped and do not affect the exit code.
