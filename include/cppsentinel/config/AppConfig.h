#pragma once

// Immutable application configuration produced by ConfigLoader (v2 schema).

#include "cppsentinel/report/Types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace cppsentinel
{

enum class EnvironmentKind : std::uint8_t
{
    Native, ///< Direct process launch (Linux / Windows)
    Msys2   ///< Windows-only: bash -l + MSYSTEM
};

/// Named execution environment referenced by checker.environment.
struct EnvironmentProfile
{
    std::string name;
    EnvironmentKind kind = EnvironmentKind::Native;
    std::filesystem::path root; ///< MSYS2 install root when kind == Msys2
    std::string msystem;        ///< MINGW64 / CLANG64 / UCRT64 / ...
    bool inherit_parent = true;
    std::map<std::string, std::string> env; ///< Extra KEY=VALUE overlays
};

/// One [[checker]] entry, in declaration order inside AppConfig::checkers.
struct CheckerConfig
{
    std::string id;   ///< Unique; used for log and artifact file names
    std::string kind; ///< Registry key, e.g. "clang-tidy"
    bool enabled = true;
    std::string environment = "native";
    std::vector<std::string> extra_args;
    /// Checker-specific optional scalars (checks, style, executable, ...).
    std::map<std::string, std::string> options;
    /// 0 means “use RunSettings::timeout_seconds”.
    std::chrono::seconds timeout{0};
};

struct ProjectConfig
{
    std::filesystem::path root;
    std::filesystem::path compilation_database; ///< compile_commands.json path
    std::filesystem::path report_dir;           ///< parent of report/<run-id>/
};

struct RunSettings
{
    int max_parallel = 1; ///< Number of checkers to run concurrently
    std::chrono::seconds timeout_seconds{120};
    FailOn fail_on = FailOn::Warning;
};

/// Maximum accepted run.max_parallel (guards against absurd thread counts).
constexpr int kMaxParallel = 16;

/// Which findings are dropped before reporting, and why.
struct FilterSettings
{
    std::vector<std::string> ignore_rules;    ///< glob, e.g. "modernize-*"
    std::vector<std::string> ignore_paths;    ///< glob vs project-relative path
    std::vector<std::string> ignore_messages; ///< case-insensitive substring
    bool has_min_severity = false;
    Severity min_severity = Severity::Unknown; ///< drop findings below this level
    bool inline_suppressions = true;           ///< honour cppsentinel-disable comments
};

/// Baseline (known findings) comparison settings.
struct BaselineSettings
{
    std::filesystem::path path;   ///< empty = baseline disabled
    bool fail_on_new_only = true; ///< exit code considers only new findings
    bool update = false;          ///< rewrite the baseline from this run
};

/// Fully resolved, validated configuration for one Engine::run.
struct AppConfig
{
    std::uint32_t schema_version = 2;
    std::filesystem::path config_path;
    ProjectConfig project;
    RunSettings run;
    FilterSettings filter;
    BaselineSettings baseline;
    std::map<std::string, EnvironmentProfile> environments;
    std::vector<CheckerConfig> checkers; ///< Declaration order preserved
};

} // namespace cppsentinel
