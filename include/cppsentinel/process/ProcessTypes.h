#pragma once

// Structured process launch request/result (no shell command strings).

#include "cppsentinel/report/Types.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cppsentinel
{

/// argv-style process description consumed by ProcessRunner implementations.
struct ProcessRequest
{
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::map<std::string, std::string> environment; ///< Overlay on parent env
    std::chrono::seconds timeout{120};
    std::filesystem::path stdout_log;
    std::filesystem::path stderr_log;
    bool inherit_parent_env = true;
};

struct ProcessResult
{
    ProcessState state = ProcessState::StartError;
    std::optional<int> exit_code;
    std::chrono::milliseconds duration{0};
    std::filesystem::path stdout_log;
    std::filesystem::path stderr_log;
    std::string error_message;
};

} // namespace cppsentinel
