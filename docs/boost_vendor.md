# Vendoring Boost with bcp

> 中文版：[boost_vendor.zh-CN.md](boost_vendor.zh-CN.md)

CppSentinel uses **Boost.Process v2** (plus Asio/System and other deps) to launch
subprocesses, capture exit codes, and enforce timeouts. Headers and Process
sources are vendored under:

```text
third_party/boost/          # bcp output: boost/ headers + libs/process sources
```

No system Boost install is required to build. Process `.cpp` files are compiled
into `cppsentinel_core`.

## Why bcp?

[`bcp`](https://www.boost.org/doc/libs/release/tools/bcp/doc/html/index.html)
copies only the files actually required by the listed modules.

## Refresh (Windows / MSYS2 MinGW)

Requirements:

- An extracted Boost release tree (e.g. 1.91.0)
- MinGW g++ via MSYS2 that can build `tools/bcp`

```powershell
.\scripts\vendor_boost.ps1 `
  -BoostSource "C:\path\to\boost_1_91_0" `
  -Msys2Root "C:\msys64"
```

| Parameter | Meaning |
|-----------|---------|
| `-BoostSource` | Boost root (`boost/`, `libs/`) |
| `-OutDir` | Output dir (default `third_party/boost`) |
| `-Modules` | bcp modules (default `process`) |
| `-Msys2Root` | MSYS2 root; else `MSYS2_ROOT` / `MSYS2_PATH`, then `C:\msys64` |

The script bootstraps/finds `bcp`, extracts modules, and writes `BCP_VERSION.txt`.

## CMake integration

Source lists and macros live in [`cmake/BoostProcess.cmake`](../cmake/BoostProcess.cmake):

```cmake
include(cmake/BoostProcess.cmake)
add_library(cppsentinel_core STATIC ... ${CPPSENTINEL_BOOST_PROCESS_SOURCES})
cppsentinel_configure_boost_process(cppsentinel_core)
```

| Macro | Purpose |
|-------|---------|
| `BOOST_PROCESS_USE_STD_FS` | Use `std::filesystem` at runtime |
| `BOOST_PROCESS_NO_LIB` / `BOOST_ALL_NO_LIB` | Disable autolink |
| `BOOST_PROCESS_STATIC_LINK` | Static link semantics |
| `BOOST_PROCESS_SOURCE` | Applied only to Process `.cpp` files |

On Windows the target also links `ws2_32`, `ntdll`, `shell32`, `advapi32`,
`user32`, and `bcrypt`.

## What is vendored

Primary module: `process` (Boost.Process v2).

Typical transitive headers: `asio`, `system`, `config`, `core`, `assert`,
`winapi`, `mp11`, … `filesystem` headers may appear, but runtime still uses
`std::filesystem`.

Implementation sources: `third_party/boost/libs/process/src/`.
