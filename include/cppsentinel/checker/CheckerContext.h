#pragma once

// Per-run, read-only context passed to Checker::run by Engine.

#include "cppsentinel/config/AppConfig.h"
#include "cppsentinel/process/ProcessRunner.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace cppsentinel
{

/// Everything a checker needs without owning report-root or exit-code policy.
struct CheckerContext
{
    const AppConfig* config = nullptr;
    const CheckerConfig* checker = nullptr;
    const EnvironmentProfile* environment = nullptr;
    ProcessRunnerPtr runner;
    std::filesystem::path target;             ///< CLI --target
    std::vector<std::filesystem::path> files; ///< expanded target files
    std::filesystem::path project_root;
    std::filesystem::path report_dir;    ///< report/<run-id>/
    std::filesystem::path logs_dir;      ///< report/<run-id>/logs/
    std::filesystem::path artifacts_dir; ///< report/<run-id>/artifacts/<id>/
    std::filesystem::path compilation_database;
    std::chrono::seconds timeout{120};
};

} // namespace cppsentinel
