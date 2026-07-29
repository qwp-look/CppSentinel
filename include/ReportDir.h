#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace util
{

/// 已成功创建的报告目录。
struct ReportDir
{
    std::filesystem::path path; ///< report/<时间戳>/ 的绝对路径
};

/// 在 baseDir 下创建 report/<年_月_日_时_分_秒_毫秒>/。
///
/// - 将 baseDir 解析为绝对路径
/// - create_directories 后校验确实是目录
/// - 用临时文件探测可写性
/// - 同一毫秒冲突时追加 _1、_2、...
std::optional<ReportDir>
createTimestampedReportDir(const std::filesystem::path& baseDir,
                           std::string& errorMessage);

/// 生成 reportDir/<净化后的Method名><扩展名>（默认 ".out"）。
std::filesystem::path
makeMethodOutputPath(const std::filesystem::path& reportDir,
                     const std::string& methodName,
                     std::string_view extension = ".out");

} // namespace util
