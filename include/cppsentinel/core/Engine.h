#pragma once

// Core orchestrator: builds the report tree, runs checkers in order, applies
// fail_on.

#include "cppsentinel/config/AppConfig.h"
#include "cppsentinel/core/CheckerRegistry.h"
#include "cppsentinel/process/ProcessRunner.h"
#include "cppsentinel/report/Findings.h"
#include "cppsentinel/report/Types.h"

#include <filesystem>
#include <memory>

namespace cppsentinel
{

/// Input to Engine::run. default_runner overrides env-based runner selection
/// (tests).
struct RunRequest
{
    AppConfig config;
    std::filesystem::path target;
    ProcessRunnerPtr default_runner;
};

/// Owns the checker registry and produces a complete RunReport (and on-disk
/// report/).
class Engine
{
  public:
    explicit Engine(CheckerRegistry registry = {});

    /// Create report dir, run enabled checkers, write every report artifact.
    /// Never throws for per-checker tool failures; may record StartError
    /// entries.
    RunReport run(const RunRequest& request) const;

    /// Same, plus the normalized/filtered/baseline-matched layer that the
    /// artifacts were written from (used by the CLI for its console summary).
    RunReport run(const RunRequest& request, UnifiedReport& unifiedOut) const;

  private:
    CheckerRegistry registry_;
};

/// Map a finished RunReport to process exit code 0 / 1 / 2 using fail_on.
int exitCodeFor(const RunReport& report, FailOn failOn);

} // namespace cppsentinel
