// Text file helpers and local timestamps for reports / run ids.

#include "cppsentinel/util/FileIo.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cppsentinel
{

std::optional<std::string> readTextFile(const std::filesystem::path& path,
                                        std::string& errorMessage)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        errorMessage = "failed to open file for read: " + path.string();
        return std::nullopt;
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    if (!in.good() && !in.eof())
    {
        errorMessage = "failed while reading: " + path.string();
        return std::nullopt;
    }
    return oss.str();
}

bool writeTextFile(const std::filesystem::path& path, std::string_view content,
                   std::string& errorMessage)
{
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            errorMessage = "failed to create parent for " + path.string() + ": " +
                           ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
    {
        errorMessage = "failed to open file for write: " + path.string();
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good())
    {
        errorMessage = "failed while writing: " + path.string();
        return false;
    }
    return true;
}

namespace
{

std::tm localTm(std::time_t t)
{
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local;
}

} // namespace

std::string nowIsoLocal()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    const auto tm = localTm(t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
        << std::setw(3) << ms.count();
    return oss.str();
}

std::string makeRunIdTimestamp()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    const auto tm = localTm(t);
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << '_'
        << std::setw(2) << (tm.tm_mon + 1) << '_' << std::setw(2) << tm.tm_mday
        << '_' << std::setw(2) << tm.tm_hour << '_' << std::setw(2) << tm.tm_min
        << '_' << std::setw(2) << tm.tm_sec << '_' << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace cppsentinel
