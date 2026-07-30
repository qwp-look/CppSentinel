# 使用 bcp 内嵌 Boost

> 英文版：[boost_vendor.md](boost_vendor.md)

CppSentinel 使用 **Boost.Process v2**（以及 Asio / System 等依赖）启动子进程、
捕获退出码并支持超时。相关头文件与 Process 源码通过 bcp 内嵌到：

```text
third_party/boost/          # bcp 输出：boost/ 头文件 + libs/process 源码
```

因此构建**不需要**系统安装的 Boost 头文件或库。
Process 的 `.cpp` 由 `cppsentinel_core` 一并编译。

## 为什么用 bcp？

[`bcp`](https://www.boost.org/doc/libs/release/tools/bcp/doc/html/index.html)
只复制模块列表真正需要的 Boost 文件，而不是整包 Boost。

## 一键刷新（Windows / MSYS2 MinGW）

前提：

- Boost 发行源码树（例如 1.91.0）
- 能编译 `tools/bcp` 的 MinGW g++（通过 MSYS2）

```powershell
# 在仓库根目录
.\scripts\vendor_boost.ps1 `
  -BoostSource "C:\path\to\boost_1_91_0" `
  -Msys2Root "C:\msys64"
```

| 参数 | 含义 |
|------|------|
| `-BoostSource` | Boost 源码根（含 `boost/`、`libs/`） |
| `-OutDir` | 输出目录，默认 `third_party/boost` |
| `-Modules` | bcp 模块列表，默认 `process` |
| `-Msys2Root` | MSYS2 根；也可设 `MSYS2_ROOT` / `MSYS2_PATH`，再回退 `C:\msys64` |

脚本会：在 Boost 树中引导/查找 `bcp` → 抽出模块 → 写入 `BCP_VERSION.txt`。

## CMake 集成

源文件列表与宏集中在 [`cmake/BoostProcess.cmake`](../cmake/BoostProcess.cmake)：

```cmake
include(cmake/BoostProcess.cmake)
# ...
add_library(cppsentinel_core STATIC ... ${CPPSENTINEL_BOOST_PROCESS_SOURCES})
cppsentinel_configure_boost_process(cppsentinel_core)
```

关键定义：

| 宏 | 作用 |
|----|------|
| `BOOST_PROCESS_USE_STD_FS` | 运行时使用 `std::filesystem` |
| `BOOST_PROCESS_NO_LIB` / `BOOST_ALL_NO_LIB` | 关闭自动链接 |
| `BOOST_PROCESS_STATIC_LINK` | 静态链接语义 |
| `BOOST_PROCESS_SOURCE` | 仅加在 Process `.cpp` 上 |

Windows 下还会链接 `ws2_32`、`ntdll`、`shell32`、`advapi32`、`user32`、`bcrypt`。

## 内嵌了什么

主模块：`process`（Boost.Process v2）。

常见传递依赖头（不完全列举）：`asio`、`system`、`config`、`core`、`assert`、
`winapi`、`mp11` 等。可能出现 `filesystem` 头文件，但运行时仍走 `std::filesystem`。

实现代码位于 `third_party/boost/libs/process/src/`（含 Windows/POSIX 细节与 ext 源）。
