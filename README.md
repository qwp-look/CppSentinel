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

## Report layout

```text
report/<run-id>/
  report.json
  summary.txt
  logs/<checker-id>.stdout.log
  logs/<checker-id>.stderr.log
  artifacts/<checker-id>/
```

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
