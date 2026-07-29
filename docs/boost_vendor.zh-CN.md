# 使用 bcp 内嵌 Boost

> 英文版：[boost_vendor.md](boost_vendor.md)

CppSentinel 使用 **Boost.Process v2**（以及它依赖的 Asio / System 等）。
相关头文件与少量 Process 源文件通过 bcp 内嵌到：

```text
third_party/boost/          # bcp 输出：boost/ 头文件 + libs/ 源码
```

因此构建时不需要完整安装系统 Boost **头文件**。
Process 的 `.cpp` 会编进 `CppSentinel` 目标。

## 为什么用 bcp？

[`bcp`](https://www.boost.org/doc/libs/release/tools/bcp/doc/html/index.html)
只复制模块列表**真正需要**的 Boost 文件，而不是整包 Boost。

## 一键刷新（Windows / MSYS2 MinGW）

前提：

- Boost 发行源码树（与目标版本一致，例如 1.91.0）
- 能编译 `tools/bcp` 的 MinGW g++

```powershell
# 在仓库根目录
.\scripts\vendor_boost.ps1 -BoostSource "C:\path\to\boost_1_91_0"
```

或手动：

```powershell
cd C:\path\to\boost_1_91_0

# 1) 编译 bcp
.\bootstrap.bat gcc
.\b2 tools/bcp toolset=gcc

# 2) 抽出 CppSentinel 需要的模块
#    process 会自动带上 asio/system/…
$out = "C:\path\to\CppSentinel\third_party\boost"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $out | Out-Null

.\dist\bin\bcp.exe `
  --boost=C:\path\to\boost_1_91_0 `
  process `
  $out
```

常用 bcp 参数：

| 参数 | 作用 |
|------|------|
| `--boost=<root>` | 含 `boost/` 与 `libs/` 的 Boost 根目录 |
| `--list` | 只列出文件，不复制 |
| `--cvs` | 包含 CVS/git 额外文件（一般不需要） |

## 内嵌了什么

主模块：

- `process`（Boost.Process v2）

常见传递依赖头文件（不完全列举）：

- `asio`、`system`、`config`、`core`、`assert`、`winapi`、`mp11` …
- 可能出现 `filesystem` 头文件；工程定义了
  `BOOST_PROCESS_USE_STD_FS`，**运行时使用 `std::filesystem`**。

`third_party/boost/libs/process/src/` 下的 Process `.cpp` 由根目录
`CMakeLists.txt` 编译。

## CMake 集成

```cmake
target_include_directories(CppSentinel PRIVATE
    third_party          # toml.hpp
    third_party/boost    # 内嵌 boost/ ...
)

target_compile_definitions(CppSentinel PRIVATE
    BOOST_PROCESS_USE_STD_FS=1
    BOOST_PROCESS_NO_LIB=1
)
```

`BOOST_PROCESS_NO_LIB` 关闭 MSVC 自动链接 pragma。
源文件显式列入目标，MinGW / MSVC 行为一致。

## 版本策略

- 在 `third_party/boost/BCP_VERSION.txt` 记录 Boost 版本。
- 升级 Process API 时优先重新跑 bcp。
- **不要**混用 A 版本内嵌头文件与 B 版本系统 `libboost_process`。

## 许可证

Boost 使用 **Boost Software License 1.0**。bcp 树中通常带有
`LICENSE_1_0.txt`。
