// ReportDir.cpp
// 创建带时间戳、可写校验的报告目录，以及每个 method 的输出路径。

#include "ReportDir.h"

#include "PathUtil.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace util
{
namespace
{

// 本地时间格式：年_月_日_时_分_秒_毫秒
std::string formatLocalTimestamp()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t now_c = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;

    std::tm local_tm{};
#ifdef _WIN32
    if (localtime_s(&local_tm, &now_c) != 0)
    {
        return {};
    }
#else
    if (localtime_r(&now_c, &local_tm) == nullptr)
    {
        return {};
    }
#endif

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (local_tm.tm_year + 1900) << '_'
        << std::setw(2) << (local_tm.tm_mon + 1) << '_' << std::setw(2)
        << local_tm.tm_mday << '_' << std::setw(2) << local_tm.tm_hour << '_'
        << std::setw(2) << local_tm.tm_min << '_' << std::setw(2)
        << local_tm.tm_sec << '_' << std::setw(3) << ms.count();
    return oss.str();
}

// 写探测文件，确认目录可写。
bool directoryIsWritable(const std::filesystem::path& dir,
                         std::string& errorMessage)
{
    namespace fs = std::filesystem;

    const fs::path probe = dir / ".cpp_sentinel_write_test";
    {
        std::ofstream probeFile(probe, std::ios::out | std::ios::trunc);
        if (!probeFile)
        {
            errorMessage = "报告目录不可写: " + dir.string();
            return false;
        }
        probeFile << "ok\n";
        if (!probeFile.good())
        {
            errorMessage = "无法在报告目录写入探测文件: " + dir.string();
            return false;
        }
    }

    std::error_code removeEc;
    fs::remove(probe, removeEc);
    // 探测文件删除失败可忽略。
    return true;
}

} // namespace

std::optional<ReportDir>
createTimestampedReportDir(const std::filesystem::path& baseDir,
                           std::string& errorMessage)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path absoluteBase = fs::absolute(baseDir, ec);
    if (ec)
    {
        errorMessage =
            "无法解析报告根目录 '" + baseDir.string() + "': " + ec.message();
        return std::nullopt;
    }

    const std::string stamp = formatLocalTimestamp();
    if (stamp.empty())
    {
        errorMessage = "无法格式化本地时间戳以创建报告目录";
        return std::nullopt;
    }

    const fs::path target = absoluteBase / stamp;

    for (int attempt = 0; attempt < 16; ++attempt)
    {
        fs::path candidate = target;
        if (attempt > 0)
        {
            candidate += "_" + std::to_string(attempt);
        }

        ec.clear();
        fs::create_directories(candidate, ec);

        // create_directories 在目录已存在时可能返回 false；
        // 成功标准：无 error_code 且路径确实是目录。
        if (ec)
        {
            if (ec == std::errc::file_exists)
            {
                continue;
            }
            errorMessage = "创建报告目录失败 '" + candidate.string() +
                           "': " + ec.message();
            return std::nullopt;
        }

        std::error_code checkEc;
        if (!fs::is_directory(candidate, checkEc) || checkEc)
        {
            errorMessage = "创建后路径不是目录: " + candidate.string();
            if (checkEc)
            {
                errorMessage += " (" + checkEc.message() + ")";
            }
            return std::nullopt;
        }

        if (!directoryIsWritable(candidate, errorMessage))
        {
            return std::nullopt;
        }

        ReportDir result;
        result.path = candidate;
        return result;
    }

    errorMessage = "在以下目录下重试耗尽，仍无法创建唯一报告目录: " +
                   absoluteBase.string();
    return std::nullopt;
}

std::filesystem::path
makeMethodOutputPath(const std::filesystem::path& reportDir,
                     const std::string& methodName, std::string_view extension)
{
    std::string fileName = sanitizeFileToken(methodName);
    if (!extension.empty() && extension.front() != '.')
    {
        fileName.push_back('.');
    }
    fileName.append(extension.begin(), extension.end());
    return reportDir / fileName;
}

} // namespace util
