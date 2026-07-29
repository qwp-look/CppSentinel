#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "RunResult.h"

namespace util
{

/// 通过 MSYSTEM 选择的 MSYS2 登录环境。
enum class MsysEnvironment : std::uint8_t
{
    Mingw64,
    Clang64,
    Ucrt64,
    Clangarm64,
    Msys
};

/// 将配置字符串（如 "mingw64"）解析为枚举。
std::optional<MsysEnvironment> parseMsysEnvironment(std::string_view name);

/// 日志用的小写环境名。
std::string_view msysEnvironmentName(MsysEnvironment env);

/// 写入 MSYSTEM 的值（如 "MINGW64"），供 bash -l 使用。
std::string_view msystemValue(MsysEnvironment env);

/// 已定位到的 MSYS2 安装路径。
struct Msys2Install
{
    std::filesystem::path root; ///< 例如 C:\msys64
    std::filesystem::path bash; ///< root/usr/bin/bash.exe
};

/// 查找 MSYS2：优先 MSYS2_ROOT / MSYS2_PATH，再尝试常见默认路径。
std::optional<Msys2Install> findMsys2Install(std::string& errorMessage);

/// 在指定 MSYS2 环境中执行 command：
///   bash.exe -l -c "<command>"
/// 并设置 MSYSTEM / CHERE_INVOKING / MSYS2_PATH_TYPE。
/// 不经过 cmd.exe + msys2_shell.cmd，避免嵌套引号问题。
RunResult runInMsys2(MsysEnvironment env, std::string_view command,
                     const std::optional<Msys2Install>& install = std::nullopt);

} // namespace util
