# Architecture (v2)

> 中文版：[architecture.zh-CN.md](architecture.zh-CN.md)

CppSentinel v2 is a **configurable C++ audit orchestrator**. It does not perform
static analysis itself. It:

1. Loads and validates a v2 TOML config
2. Creates built-in checkers from a registry
3. Runs tools through Native or MSYS2 process runners
4. Collects diagnostics into a structured `RunReport` and writes a report tree

## Pipeline

```text
CLI: run --config --target
        │
        ▼
 ConfigLoader  ──► AppConfig (or ConfigDiagnostic list)
        │
        ▼
 Engine
   create exclusive report/<run-id>/
   for each [[checker]] in order:
     - disabled → Skipped
     - CheckerRegistry::create(kind)
     - pick ProcessRunner (native / msys2; tests may inject Fake)
     - Checker::run(CheckerContext)
         → build ProcessRequest (exe + argv)
         → ProcessRunner::run
         → parse stdout/stderr logs → Diagnostic[]
     - append CheckerResult
        │
        ▼
 ReportWriter  ──► report.json (atomic publish) + summary.txt + logs/ + artifacts/
        │
        ▼
 Console summary + exitCodeFor(fail_on)
```

## Source layout

```text
main.cpp                         CLI entry
include/cppsentinel/
  config/                        AppConfig, ConfigLoader
  core/                          Engine, CheckerRegistry
  checker/                       Checker API + helper headers
  process/                       ProcessRequest, runners, path/env
  report/                        Diagnostic, RunReport, reporters
  util/                          file IO, timestamps
src/                             matching implementations
cmake/BoostProcess.cmake         vendored Boost.Process sources
Configuration/Configuration.toml default v2 config
tests/unit|integration           CTest
```

## Module responsibilities

| Module | Responsibility |
|--------|----------------|
| `main.cpp` | CLI only: parse `run --config --target`, load config, run engine, print summary |
| `config` | TOML parse + schema validation + path resolution → immutable `AppConfig` |
| `core` | `Engine` orchestration; `CheckerRegistry` creates checkers by `kind` |
| `checker` | Built-in checkers; `CompileCommands` / `DiagnosticParser` |
| `process` | `ProcessRequest/Result`; Native, MSYS2, and Fake runners |
| `report` | `RunReport`; JSON/console; exclusive report dirs and log paths |

**Boundaries:**

- Checkers do **not** print the run summary, create the report root, or choose the process exit code.
- Engine does **not** parse TOML, parse tool output, or build shell command strings.
- Paths stay as `std::filesystem::path` in Core/Checker; MSYS POSIX conversion happens only in the MSYS2 runner.

## Process model

- **Native** (Linux / Windows): direct `executable + arguments`, stdout/stderr to log files, timeout + terminate.
- **MSYS2** (Windows only):

```text
<MSYS2>/usr/bin/bash.exe -l -c 'exec "$@"' cppsentinel-tool <exe> <arg...>
```

No `cmd.exe` / `msys2_shell.cmd`. Arguments remain separate argv entries.

Child environment = optional parent copy + profile/`ProcessRequest` overlays; MSYS2 forces
`MSYSTEM`, `CHERE_INVOKING=1`, `MSYS2_PATH_TYPE=inherit`.

## Reports and failure policy

```text
report/<run-id>/
  report.json
  summary.txt
  logs/<id>.stdout.log
  logs/<id>.stderr.log
  artifacts/<id>/
```

`run-id` is a local timestamp (with milliseconds); collisions get a suffix. Existing
directories are **never** reused.

Hard statuses (`TimedOut`, `StartError`, `ToolError`, `ConfigError`) always fail.
Soft completion with diagnostics is governed by `[run].fail_on`.

| Exit | Meaning |
|------|---------|
| 0 | Pass under `fail_on` |
| 1 | Startup failure (CLI/config/report dir/write) |
| 2 | At least one checker failed the policy |

## Extending

Add a checker **without** editing `Engine`:

1. Implement `Checker` (`kind()` + `run(const CheckerContext&)`)
2. Register a factory in `registerBuiltinCheckers()`
3. Use the new `kind` in config and add tests

Registration is compile-time only. No dynamic plugins; no parallel checkers yet
(`max_parallel` must be 1).
