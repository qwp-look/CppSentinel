// Tool version probing (see ToolProbe.h).

#include "cppsentinel/core/ToolProbe.h"

#include "cppsentinel/util/FileIo.h"

#include <string>
#include <string_view>

namespace cppsentinel
{
namespace
{

std::string firstVersionLine(std::string_view text)
{
    std::string fallback;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.remove_suffix(1);
        }
        while (!line.empty() &&
               (line.front() == ' ' || line.front() == '\t'))
        {
            line.remove_prefix(1);
        }
        if (line.empty())
        {
            continue;
        }
        if (fallback.empty())
        {
            fallback = std::string(line);
        }
        // Prefer the line that actually names a version: clang prints
        // "LLVM (http://llvm.org/):" before "LLVM version 22.1.8".
        std::string lowered;
        lowered.reserve(line.size());
        for (char c : line)
        {
            lowered.push_back(
                (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
        if (lowered.find("version") != std::string::npos)
        {
            return std::string(line);
        }
    }
    return fallback;
}

} // namespace

std::string probeToolVersion(const ProcessRunnerPtr& runner,
                             const std::string& executable,
                             const std::filesystem::path& workingDirectory,
                             std::chrono::seconds timeout,
                             const std::filesystem::path& logPath)
{
    if (!runner || executable.empty())
    {
        return {};
    }

    ProcessRequest request;
    request.executable = executable;
    request.arguments = {"--version"};
    request.working_directory = workingDirectory;
    request.timeout = timeout;
    if (!logPath.empty())
    {
        request.stdout_log = logPath;
        request.stdout_log += ".stdout.log";
        request.stderr_log = logPath;
        request.stderr_log += ".stderr.log";
    }

    const auto result = runner->run(request);
    if (result.state == ProcessState::StartError)
    {
        return {};
    }

    std::string ignored;
    if (!result.stdout_log.empty())
    {
        if (auto text = readTextFile(result.stdout_log, ignored))
        {
            auto line = firstVersionLine(*text);
            if (!line.empty())
            {
                return line;
            }
        }
    }
    if (!result.stderr_log.empty())
    {
        if (auto text = readTextFile(result.stderr_log, ignored))
        {
            return firstVersionLine(*text);
        }
    }
    return {};
}

} // namespace cppsentinel
