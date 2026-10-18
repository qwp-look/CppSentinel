#pragma once

// `<tool> --version` probing, shared by the run metadata and `doctor`.

#include "cppsentinel/process/ProcessRunner.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace cppsentinel
{

/// Run `<executable> --version` through `runner` and return its first non-empty
/// line. Returns an empty string when the tool cannot be started or prints
/// nothing useful. `logPath` (optional) receives stdout/stderr with
/// `.stdout.log` / `.stderr.log` suffixes.
std::string probeToolVersion(const ProcessRunnerPtr& runner,
                             const std::string& executable,
                             const std::filesystem::path& workingDirectory,
                             std::chrono::seconds timeout,
                             const std::filesystem::path& logPath = {});

} // namespace cppsentinel
