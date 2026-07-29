#pragma once

#include <string>

#include "Method.h"

namespace util
{

/// 展开 method.command 中的占位符。
///
/// - {file_in}  -> fileIn
/// - {file_out} -> fileOut
///
/// forMsys 为 true 时，两条路径都会转成 MSYS POSIX 形式
/// （例如 C:\proj\a.cpp -> /c/proj/a.cpp），以便 bash 重定向正常工作。
std::string buildCommand(const Method& method, const std::string& fileIn,
                         const std::string& fileOut, bool forMsys = true);

} // namespace util
