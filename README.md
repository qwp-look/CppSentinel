# CppSentinel

Configurable **C++ code-audit orchestrator** (v2). It loads checkers from a TOML
config, runs them through a process layer (Windows MSYS2 or native), and writes a
structured report under `report/<run-id>/`.

> 中文版：[README.zh-CN.md](README.zh-CN.md)

## Features

- Declarative `[[checker]]` list (clang-tidy, clang-format, compiler diagnostics, cppcheck)
- No raw shell command templates in default config
- Unified `ProcessRequest` execution (Native + MSYS2; separate argv, no shell join)
- Machine-readable `report.json`, raw logs, console summary
- Vendored Boost.Process via bcp (no system Boost required to build)
- MinGW builds link the runtime statically so the binary starts from plain PowerShell

## Dependencies

- CMake 3.16+
- C++20 compiler (MinGW-w64 / MSVC / clang / g++)
- Tools used by enabled checkers (typically via MSYS2 on Windows)
- Build does **not** require a system Boost install (`third_party/boost`)

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`CMAKE_EXPORT_COMPILE_COMMANDS` is on by default so the build directory gets a
`compile_commands.json` for `clang-tidy` / `compiler-diagnostics`.

Optional: `-DCPPSENTINEL_BUILD_TESTS=OFF` to skip tests.

## Usage

```bash
CppSentinel run --config <Configuration.toml> --target <file-or-dir>
```

| Flag | Meaning |
|------|---------|
| `--config` | Path to a v2 config file (required, explicit) |
| `--target` | File or directory to check (semantics depend on each checker) |
| `--baseline` | Baseline file; only findings absent from it count as new |
| `--update-baseline` | Rewrite the baseline from this run |
| `--jobs` | Number of checkers to run in parallel (1..16) |

Subcommands: `run` (audit), `doctor` (environment checks), `init` (write a config template).

Example:

```bash
./build/CppSentinel.exe run --config ./Configuration/Configuration.toml --target ./main.cpp
```

### Environment variables

| Variable | Meaning |
|----------|---------|
| `MSYS2_ROOT` | MSYS2 install root (must contain `usr/bin/bash.exe`) |
| `MSYS2_PATH` | Alternate root |

If unset, the MSYS2 runner tries common defaults (e.g. `C:\msys64`) or
`[environment.*.root]` from the config.

## Configuration

- Default: [`Configuration/Configuration.toml`](Configuration/Configuration.toml)
- Guide: [`docs/configuration.md`](docs/configuration.md)

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

Relative paths are resolved against the **config file directory**. Change
`compilation_database` if your build dir is not `build`.

## Targets

`--target` accepts a file or a directory. A directory is expanded once, before
any tool runs, so clang-tidy, clang-format, compiler-diagnostics and cppcheck
all analyse exactly the same files:

- C/C++ extensions only (`.c .cc .cpp .cxx .h .hh .hpp .hxx .ipp .inl ...`)
- `build`, `build-*`, `node_modules` and dot-directories are skipped
- unreadable subtrees stop the walk and are reported as a note
- `[filter].ignore_paths` globs also exclude files from analysis

Checkers then invoke their tool in batches of up to 40 files (clang-format runs
per file so a failure is attributable), and `[run].max_parallel` (1..16) decides
how many checkers run at once. Result order always follows the `[[checker]]`
declaration order, so reports are reproducible.

## Filtering and baselines

Not every finding is worth acting on. Filters drop findings explicitly and
record why, instead of hiding them:

```toml
[filter]
ignore_rules = ["modernize-*"]        # glob on rule id
ignore_paths = ["third_party/**"]     # glob on project-relative path
ignore_messages = ["is too short"]    # case-insensitive substring
min_severity = "warning"              # drop style/information
inline_suppressions = true
```

In-source directives:

```cpp
// cppsentinel-disable-next-line readability-identifier-length
int x;
// cppsentinel-disable cert-err33-c        <- rest of the file
```

Suppressed findings are listed in `report.json` under `suppressed` with a
reason (`config:ignore_rules`, `inline:disable`, ...) and counted in the summary.

Baselines turn existing debt into an accepted set:

```bash
CppSentinel run --config ... --target src --update-baseline
CppSentinel run --config ... --target src --baseline Configuration/baseline.json
```

With a baseline only `new` findings can fail the run; entries that vanished are
reported as `fixed`. Matching uses the stable id first, then file + rule +
message, so a finding that only moved lines stays known.

## Report layout

Each run emits one normalized report instead of concatenated tool output:

```text
report/<run-id>/
  report.json              # unified findings (schema 3) - machine source of truth
  report.raw.json          # per-checker raw diagnostics (schema 2) - traceability
  report.sarif             # SARIF 2.1.0 for code scanning / IDE consumers
  report.md                # human report: overview, tool runs, per-file findings
  summary.txt              # console summary (same text as stdout)
  logs/<checker-id>.stdout.log
  logs/<checker-id>.stderr.log
  artifacts/<checker-id>/
```

Normalization applied by the unified layer:

- **Paths** made project-relative with `/` separators, so tools agree on one file key
- **Rule/category** unified (`bugprone-*` → defect, `cert-*` → security, ...)
- **Duplicates collapsed** (a header reported once per translation unit becomes one
  finding with an `occurrences` count and a list of contributing tools)
- **Stable ids** (`CS-<hash>`) that stay constant across runs, usable for baselines
- **`note:` lines** attached to the diagnostic they annotate instead of being
  counted as separate findings
- **Tool meta output** (`checkersReport`, `unmatchedSuppression`, ...) moved out of
  the finding list into `notes`
- **Trust warnings** when a checker timed out or failed to start, so a partial run
  is never mistaken for a clean one
- **Provenance**: each tool's `--version` is recorded in `metadata.tool_versions`
- **Snippets**: the offending source line travels with the finding
- **Fix-its**: clang-tidy replacements become `fixes[]` in `report.json` and
  `fixes` in SARIF, so results are machine-applicable

`report.json` is `summary` (severity/category/file/rule rollups) plus `findings`.

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success under configured `fail_on` |
| 1 | Startup / configuration / report directory or write failure |
| 2 | At least one checker failed the policy |

Disabled checkers are skipped and do not fail the run by themselves.

## Layout

```text
main.cpp                      CLI
include/cppsentinel/          public headers (config/core/checker/process/report)
src/                          implementations
cmake/BoostProcess.cmake      vendored Boost.Process wiring
Configuration/                default Configuration.toml
tests/                        unit + integration (CTest)
docs/                         architecture / configuration / boost notes
scripts/vendor_boost.ps1      bcp refresh script
third_party/toml.hpp          toml++
third_party/boost/            bcp-extracted Boost.Process
report/                       run outputs (contents gitignored)
```

## Docs

| Doc | Description |
|-----|-------------|
| [docs/README.md](docs/README.md) | Doc index |
| [architecture.md](docs/architecture.md) | Pipeline and modules |
| [configuration.md](docs/configuration.md) | Schema and checker options |
| [boost_vendor.md](docs/boost_vendor.md) | bcp vendoring |

## License

Project code: [LICENSE](LICENSE).  
Vendored Boost: [Boost Software License 1.0](third_party/boost/LICENSE_1_0.txt).
