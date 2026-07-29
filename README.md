# CppSentinel

Configurable **C++ code-audit orchestrator**. It loads check methods from
`Configuration.toml`, runs them inside MSYS2 environments (`mingw64`,
`clang64`, …), and writes per-tool outputs under `report/<timestamp>/`.

> 中文文档：[README.zh-CN.md](README.zh-CN.md)

## Features

- TOML-driven tool list (clang-tidy, clang-format, cppcheck, g++/clang diagnostics, …)
- Robust MSYS2 launch: `bash -l -c` + `MSYSTEM` (no nested `cmd` / `msys2_shell` quoting)
- Timestamped, writable report directories with per-method output files
- Vendored Boost.Process headers/sources via **bcp** (no system Boost install required)

## Requirements

- CMake 3.16+
- C++20 compiler (MinGW-w64 / MSVC / clang-cl)
- MSYS2 with the tools referenced in your config
- *(Build only)* no system Boost package needed — see `third_party/boost`

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

On MSYS2 MinGW:

```bash
export PATH=/mingw64/bin:$PATH
cmake -S . -B build -G Ninja
cmake --build build
```

## Usage

```bash
CppSentinel <search_path> <check_path>
```

| Argument | Meaning |
|----------|---------|
| `search_path` | Tree to search for `Configuration.toml` (depth ≤ 3) |
| `check_path` | Value substituted into `{file_in}` |

Interactive mode (prompts for both paths):

```bash
CppSentinel
```

### Environment

| Variable | Meaning |
|----------|---------|
| `MSYS2_ROOT` | MSYS2 install root containing `usr/bin/bash.exe` |
| `MSYS2_PATH` | Alternate root candidate |

If unset, common defaults such as `C:\msys64` are tried.

### Example

```bash
./build/CppSentinel.exe ./Configuration ./main.cpp
```

## Configuration

See:

- [`Configuration/Configuration.toml`](Configuration/Configuration.toml) — default methods
- [`docs/configuration.md`](docs/configuration.md) — field reference and command tips

Placeholders:

| Token | Replaced with |
|-------|----------------|
| `{file_in}` | `check_path` (MSYS POSIX form, e.g. `/c/proj/main.cpp`) |
| `{file_out}` | `report/<timestamp>/<MethodName>.out` |

## Project layout

```text
main.cpp                 orchestration only
include/                 public headers (FileFinder, TomlParser, Msys2Runner, …)
src/                     implementations
Configuration/           default Configuration.toml
docs/                    architecture, config, boost vendor notes
third_party/toml.hpp     toml++ single header
third_party/boost/       bcp-vendored Boost.Process (+ deps)
scripts/vendor_boost.ps1 refresh vendored Boost with bcp
report/                  generated outputs (gitignored contents)
```

## Vendored Boost (bcp)

Headers and Process sources live under [`third_party/boost`](third_party/boost).
CMake compiles `libs/process/src/*.cpp` into the binary with
`BOOST_PROCESS_USE_STD_FS` (uses `std::filesystem`).

Refresh after upgrading Boost:

```powershell
.\scripts\vendor_boost.ps1 -BoostSource "C:\path\to\boost_1_91_0"
```

Details: [`docs/boost_vendor.md`](docs/boost_vendor.md).

## How execution works

```text
bash.exe -l -c "<command>"
  MSYSTEM=MINGW64|CLANG64|...
  CHERE_INVOKING=1
  MSYS2_PATH_TYPE=inherit
```

Architecture notes: [`docs/architecture.md`](docs/architecture.md).

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | All enabled methods launched and exited 0 |
| 1 | Startup/config/report-dir failure |
| 2 | One or more methods failed to launch or returned non-zero |

Disabled methods are skipped and do not affect the exit code.

## License

Project code: see [LICENSE](LICENSE).  
Vendored Boost: [Boost Software License 1.0](third_party/boost/LICENSE_1_0.txt).
