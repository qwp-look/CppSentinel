// Tool version probing (see ToolProbe.h).

#include "cppsentinel/core/ToolProbe.h"

#include "cppsentinel/process/PathUtil.h"
#include "cppsentinel/util/FileIo.h"

#include <atomic>
#include <chrono>
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
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
        {
            line.remove_suffix(1);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
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

    // The runner redirects stdio into files, so probes need somewhere to write
    // even when the caller did not ask for logs.
    std::filesystem::path stdoutPath;
    std::filesystem::path stderrPath;
    bool temporaryLogs = false;
    if (logPath.empty())
    {
        static std::atomic<unsigned> counter{0};
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        const auto stem = "cppsentinel-probe-" + std::to_string(stamp) + "-" +
                          std::to_string(counter.fetch_add(1));
        stdoutPath = std::filesystem::temp_directory_path() / (stem + ".out");
        stderrPath = std::filesystem::temp_directory_path() / (stem + ".err");
        temporaryLogs = true;
    }
    else
    {
        stdoutPath = logPath;
        stdoutPath += ".stdout.log";
        stderrPath = logPath;
        stderrPath += ".stderr.log";
    }

    ProcessRequest request;
    request.executable = resolveExecutablePath(executable);
    request.arguments = {"--version"};
    request.working_directory = workingDirectory;
    request.timeout = timeout;
    request.stdout_log = stdoutPath;
    request.stderr_log = stderrPath;

    const auto result = runner->run(request);

    std::string version;
    std::string ignored;
    if (result.state != ProcessState::StartError)
    {
        if (auto text = readTextFile(stdoutPath, ignored))
        {
            version = firstVersionLine(*text);
        }
        if (version.empty())
        {
            if (auto text = readTextFile(stderrPath, ignored))
            {
                version = firstVersionLine(*text);
            }
        }
    }

    if (temporaryLogs)
    {
        std::error_code ec;
        std::filesystem::remove(stdoutPath, ec);
        std::filesystem::remove(stderrPath, ec);
    }

    return version;
}

} // namespace cppsentinel
