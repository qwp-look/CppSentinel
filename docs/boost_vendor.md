# Vendoring Boost with bcp

> 中文版：[boost_vendor.zh-CN.md](boost_vendor.zh-CN.md)

CppSentinel uses **Boost.Process v2** (plus the Asio / System pieces it needs).
Those headers (and the small set of Process translation units) are vendored under:

```text
third_party/boost/          # bcp output: boost/ headers + libs/ sources
```

so the project can build without a full system Boost *header* install.
Compiled Process sources are built into the `CppSentinel` target.

## Why bcp?

[`bcp`](https://www.boost.org/doc/libs/release/tools/bcp/doc/html/index.html)
copies **only the Boost files required** by a module list, instead of shipping
the entire Boost distribution.

## One-shot refresh (Windows / MSYS2 MinGW)

Prerequisites:

- Boost release source tree (same major version you want to vendor), e.g. 1.91.0
- MinGW g++ able to build `tools/bcp`

```powershell
# From repo root
.\scripts\vendor_boost.ps1 -BoostSource "C:\path\to\boost_1_91_0"
```

Or manually:

```powershell
cd C:\path\to\boost_1_91_0

# 1) build bcp
.\bootstrap.bat gcc
.\b2 tools/bcp toolset=gcc

# 2) extract modules used by CppSentinel
#    process pulls asio/system/… automatically
$out = "C:\path\to\CppSentinel\third_party\boost"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $out | Out-Null

.\dist\bin\bcp.exe `
  --boost=C:\path\to\boost_1_91_0 `
  process `
  $out
```

Useful bcp flags:

| Flag | Purpose |
|------|---------|
| `--boost=<root>` | Boost root containing `boost/` and `libs/` |
| `--list` | Print files instead of copying |
| `--cvs` | Include CVS/git extras (usually not needed) |

## What we vendor

Primary module:

- `process` (Boost.Process v2)

Transitive headers commonly pulled in (non-exhaustive):

- `asio`, `system`, `config`, `core`, `assert`, `winapi`, `mp11`, …
- optional `filesystem` headers may appear; the project defines
  `BOOST_PROCESS_USE_STD_FS` so **runtime code uses `std::filesystem`**.

Process `.cpp` files under `third_party/boost/libs/process/src/` are compiled by
CMake (see root `CMakeLists.txt`).

## CMake integration

```cmake
target_include_directories(CppSentinel PRIVATE
    third_party          # toml.hpp
    third_party/boost    # vendored boost/ ...
)

target_compile_definitions(CppSentinel PRIVATE
    BOOST_PROCESS_USE_STD_FS=1
    BOOST_PROCESS_NO_LIB=1
)
```

`BOOST_PROCESS_NO_LIB` disables auto-link pragmas (MSVC).
Sources are listed explicitly so MinGW/MSVC both link the same objects.

## Versioning policy

- Record the Boost version in `third_party/boost/BCP_VERSION.txt`.
- Prefer refreshing bcp output when upgrading Process APIs.
- Do **not** mix vendored headers from version A with a system `libboost_process`
  from version B.

## License

Boost is licensed under the **Boost Software License 1.0**. A copy is included
with the bcp tree (`LICENSE_1_0.txt` when present).
