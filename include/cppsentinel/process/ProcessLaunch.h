#pragma once

// Shared Boost.Process v2 launch helper (file-backed stdio + async timeout).

#include "cppsentinel/process/ProcessTypes.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace cppsentinel
{

/// Launch executable with an explicit environment block and log file paths.
/// On timeout the child is terminated and state becomes TimedOut.
ProcessResult launchProcess(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments,
                            const std::filesystem::path& workingDirectory,
                            const std::vector<std::string>& environment,
                            std::chrono::seconds timeout,
                            const std::filesystem::path& stdoutLog,
                            const std::filesystem::path& stderrLog);

} // namespace cppsentinel
