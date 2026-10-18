#pragma once

// Shared report model: severities, checker/process status, diagnostics,
// RunReport. report.json is serialized from these types (see JsonReporter).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cppsentinel
{

/// Diagnostic severity used by fail_on policy and report.json.
enum class Severity : std::uint8_t
{
    Error,
    Warning,
    Style,
    Information,
    Unknown
};

/// Outcome of one checker invocation (independent of process exit code).
enum class CheckerStatus : std::uint8_t
{
    Succeeded,  ///< Tool finished; diagnostics may still fail the run via
                ///< fail_on
    Failed,     ///< Soft failure reserved for incomplete/unknown soft cases
    Skipped,    ///< enabled = false
    TimedOut,   ///< Hard failure
    StartError, ///< Hard failure: process could not start
    ToolError,  ///< Hard failure: non-zero exit with no parseable diagnostics
    ConfigError ///< Hard failure: missing CDB, unknown kind, bad environment,
                ///< ...
};

/// Low-level process runner outcome.
enum class ProcessState : std::uint8_t
{
    Succeeded,
    Failed,
    TimedOut,
    StartError
};

/// Role of a diagnostic line inside a tool report.
/// clang/gcc emit "note:" lines that annotate the preceding diagnostic; they
/// are context, not independent findings.
enum class DiagnosticRole : std::uint8_t
{
    Primary,
    Note
};

/// [run].fail_on threshold: diagnostics at or above this level fail the run.
enum class FailOn : std::uint8_t
{
    Never,
    Error,
    Warning,
    Style,
    Information
};

inline const char* toString(Severity s) noexcept
{
    switch (s)
    {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Style:
        return "style";
    case Severity::Information:
        return "information";
    case Severity::Unknown:
        return "unknown";
    }
    return "unknown";
}

inline const char* toString(CheckerStatus s) noexcept
{
    switch (s)
    {
    case CheckerStatus::Succeeded:
        return "succeeded";
    case CheckerStatus::Failed:
        return "failed";
    case CheckerStatus::Skipped:
        return "skipped";
    case CheckerStatus::TimedOut:
        return "timed_out";
    case CheckerStatus::StartError:
        return "start_error";
    case CheckerStatus::ToolError:
        return "tool_error";
    case CheckerStatus::ConfigError:
        return "config_error";
    }
    return "unknown";
}

inline const char* toString(ProcessState s) noexcept
{
    switch (s)
    {
    case ProcessState::Succeeded:
        return "succeeded";
    case ProcessState::Failed:
        return "failed";
    case ProcessState::TimedOut:
        return "timed_out";
    case ProcessState::StartError:
        return "start_error";
    }
    return "unknown";
}

inline const char* toString(DiagnosticRole role) noexcept
{
    switch (role)
    {
    case DiagnosticRole::Primary:
        return "primary";
    case DiagnosticRole::Note:
        return "note";
    }
    return "primary";
}

inline const char* toString(FailOn f) noexcept
{
    switch (f)
    {
    case FailOn::Never:
        return "never";
    case FailOn::Error:
        return "error";
    case FailOn::Warning:
        return "warning";
    case FailOn::Style:
        return "style";
    case FailOn::Information:
        return "information";
    }
    return "warning";
}

/// One structured finding parsed from tool output (or synthesized by a
/// checker).
struct Diagnostic
{
    std::filesystem::path file;
    std::optional<std::uint32_t> line;
    std::optional<std::uint32_t> column;
    Severity severity = Severity::Unknown;
    std::string message;
    std::string rule; ///< e.g. clang-tidy check id, cppcheck id
    DiagnosticRole role = DiagnosticRole::Primary;
};

/// Result of a single [[checker]] execution.
struct CheckerResult
{
    std::string checker_id;
    std::string checker_kind;
    CheckerStatus status = CheckerStatus::Failed;
    std::optional<int> exit_code;
    std::chrono::milliseconds duration{0};
    std::vector<Diagnostic> diagnostics;
    std::vector<std::filesystem::path> artifacts;
    std::filesystem::path stdout_log;
    std::filesystem::path stderr_log;
    std::string message;
};

/// Version of one tool as reported by `<exe> --version`.
struct ToolVersion
{
    std::string checker_id;
    std::string executable;
    std::string version; ///< first non-empty line of --version output
};

/// Run-level metadata written into report.json.
struct RunMetadata
{
    std::string run_id;
    std::filesystem::path config_path;
    std::filesystem::path target_path;
    std::filesystem::path project_root;
    std::filesystem::path report_dir;
    std::string started_at;
    std::string finished_at;
    std::chrono::milliseconds duration{0};
    std::vector<ToolVersion> tool_versions; ///< provenance for reproducibility
};

/// Full machine-readable run result (schema_version 2).
struct RunReport
{
    std::uint32_t schema_version = 2;
    RunMetadata metadata;
    std::vector<CheckerResult> checkers;
    int exit_code = 0;
};

} // namespace cppsentinel
