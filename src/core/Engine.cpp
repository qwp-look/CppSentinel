// Engine: exclusive report dir, sequential checkers, fail_on exit mapping.

#include "cppsentinel/core/Engine.h"

#include "cppsentinel/process/Msys2ProcessRunner.h"
#include "cppsentinel/process/NativeProcessRunner.h"
#include "cppsentinel/report/Baseline.h"
#include "cppsentinel/report/Filter.h"
#include "cppsentinel/report/ReportWriter.h"
#include "cppsentinel/util/FileIo.h"

#include <utility>

namespace cppsentinel
{
namespace
{

bool severityFails(Severity severity, FailOn failOn)
{
    switch (failOn)
    {
    case FailOn::Never:
        return false;
    case FailOn::Error:
        return severity == Severity::Error;
    case FailOn::Warning:
        return severity == Severity::Error || severity == Severity::Warning;
    case FailOn::Style:
        return severity == Severity::Error || severity == Severity::Warning ||
               severity == Severity::Style;
    case FailOn::Information:
        return severity == Severity::Error || severity == Severity::Warning ||
               severity == Severity::Style || severity == Severity::Information;
    }
    return false;
}

bool checkerResultFails(const CheckerResult& result, FailOn failOn)
{
    // Hard failures always fail the run.
    switch (result.status)
    {
    case CheckerStatus::Succeeded:
    case CheckerStatus::Skipped:
    case CheckerStatus::Failed:
        // Failed is treated as soft; diagnostics + fail_on decide.
        break;
    case CheckerStatus::TimedOut:
    case CheckerStatus::StartError:
    case CheckerStatus::ToolError:
    case CheckerStatus::ConfigError:
        return true;
    }

    for (const auto& d : result.diagnostics)
    {
        if (severityFails(d.severity, failOn))
        {
            return true;
        }
    }

    // Soft Failed with no diagnostics still counts as failure.
    if (result.status == CheckerStatus::Failed)
    {
        return true;
    }
    return false;
}

bool hasHardFailure(const RunReport& report) noexcept
{
    for (const auto& checker : report.checkers)
    {
        switch (checker.status)
        {
        case CheckerStatus::TimedOut:
        case CheckerStatus::StartError:
        case CheckerStatus::ToolError:
        case CheckerStatus::ConfigError:
            return true;
        case CheckerStatus::Succeeded:
        case CheckerStatus::Failed:
        case CheckerStatus::Skipped:
            break;
        }
    }
    return false;
}

ProcessRunnerPtr runnerForEnvironment(const EnvironmentProfile& profile,
                                      const ProcessRunnerPtr& overrideRunner)
{
    if (overrideRunner)
    {
        return overrideRunner;
    }
    if (profile.kind == EnvironmentKind::Msys2)
    {
        return std::make_shared<Msys2ProcessRunner>(profile);
    }
    return std::make_shared<NativeProcessRunner>();
}

CheckerResult makeSkipped(const CheckerConfig& cfg, std::string message)
{
    CheckerResult r;
    r.checker_id = cfg.id;
    r.checker_kind = cfg.kind;
    r.status = CheckerStatus::Skipped;
    r.message = std::move(message);
    return r;
}

CheckerResult makeConfigError(const CheckerConfig& cfg, std::string message)
{
    CheckerResult r;
    r.checker_id = cfg.id;
    r.checker_kind = cfg.kind;
    r.status = CheckerStatus::ConfigError;
    r.message = std::move(message);
    return r;
}

} // namespace

Engine::Engine(CheckerRegistry registry) : registry_(std::move(registry)) {}

RunReport Engine::run(const RunRequest& request) const
{
    UnifiedReport ignored;
    return run(request, ignored);
}

RunReport Engine::run(const RunRequest& request, UnifiedReport& unifiedOut) const
{
    RunReport report;
    report.schema_version = 2;
    report.metadata.config_path = request.config.config_path;
    report.metadata.target_path = request.target;
    report.metadata.project_root = request.config.project.root;
    report.metadata.started_at = nowIsoLocal();

    const auto wallStart = std::chrono::steady_clock::now();

    std::string reportError;
    auto layout =
        createReportLayout(request.config.project.report_dir, reportError);
    if (!layout)
    {
        report.metadata.finished_at = nowIsoLocal();
        report.metadata.duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - wallStart);
        report.exit_code = 1;
        CheckerResult bootstrap;
        bootstrap.checker_id = "engine";
        bootstrap.checker_kind = "engine";
        bootstrap.status = CheckerStatus::StartError;
        bootstrap.message = reportError;
        report.checkers.push_back(std::move(bootstrap));
        return report;
    }

    ReportWriter writer(*layout);
    report.metadata.run_id = layout->run_id;
    report.metadata.report_dir = layout->root;

    for (const auto& checkerCfg : request.config.checkers)
    {
        if (!checkerCfg.enabled)
        {
            report.checkers.push_back(
                makeSkipped(checkerCfg, "disabled in configuration"));
            continue;
        }

        const auto envIt =
            request.config.environments.find(checkerCfg.environment);
        if (envIt == request.config.environments.end())
        {
            report.checkers.push_back(makeConfigError(
                checkerCfg,
                "unknown environment '" + checkerCfg.environment + "'"));
            continue;
        }

        auto checker = registry_.create(checkerCfg);
        if (!checker)
        {
            report.checkers.push_back(makeConfigError(
                checkerCfg, "unknown checker kind '" + checkerCfg.kind + "'"));
            continue;
        }

        std::error_code ec;
        const auto artifactDir = writer.checkerArtifactDir(checkerCfg.id);
        std::filesystem::create_directories(artifactDir, ec);

        CheckerContext context;
        context.config = &request.config;
        context.checker = &checkerCfg;
        context.environment = &envIt->second;
        context.runner =
            runnerForEnvironment(envIt->second, request.default_runner);
        context.target = request.target;
        context.project_root = request.config.project.root;
        context.report_dir = layout->root;
        context.logs_dir = layout->logs_dir;
        context.artifacts_dir = artifactDir;
        context.compilation_database =
            request.config.project.compilation_database;
        context.timeout = checkerCfg.timeout.count() > 0
                              ? checkerCfg.timeout
                              : request.config.run.timeout_seconds;

        CheckerResult result;
        try
        {
            result = checker->run(context);
        }
        catch (const std::exception& ex)
        {
            result = {};
            result.checker_id = checkerCfg.id;
            result.checker_kind = checkerCfg.kind;
            result.status = CheckerStatus::ToolError;
            result.message = std::string("checker threw: ") + ex.what();
        }

        if (result.checker_id.empty())
        {
            result.checker_id = checkerCfg.id;
        }
        if (result.checker_kind.empty())
        {
            result.checker_kind = checkerCfg.kind;
        }
        if (result.stdout_log.empty())
        {
            result.stdout_log = writer.checkerStdoutLog(checkerCfg.id);
        }
        if (result.stderr_log.empty())
        {
            result.stderr_log = writer.checkerStderrLog(checkerCfg.id);
        }

        report.checkers.push_back(std::move(result));
    }

    report.metadata.finished_at = nowIsoLocal();
    report.metadata.duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - wallStart);
    report.exit_code = exitCodeFor(report, request.config.run.fail_on);

    // Normalize into the unified finding layer once; every artifact derives from it.
    NormalizeOptions normalize;
    normalize.project_root = request.config.project.root;
    auto unified = buildUnifiedReport(report, normalize);

    // Filters run before baseline matching: a suppressed finding is not
    // "known", it is simply not reported.
    applyFilters(unified, request.config.filter, request.config.project.root);

    const auto& baselineCfg = request.config.baseline;
    bool baselineActive = false;

    if (!baselineCfg.path.empty() && baselineCfg.update)
    {
        const auto data = baselineFromReport(unified);
        std::string baselineError;
        if (saveBaseline(baselineCfg.path, data, baselineError))
        {
            applyBaseline(unified, data, baselineCfg.path);
            baselineActive = true;
            unified.notes.push_back("baseline updated: " +
                                    baselineCfg.path.string() + " (" +
                                    std::to_string(data.findings.size()) +
                                    " findings recorded)");
        }
        else
        {
            unified.notes.push_back("baseline update failed: " + baselineError);
        }
    }
    else if (!baselineCfg.path.empty())
    {
        BaselineData data;
        std::string baselineError;
        if (loadBaseline(baselineCfg.path, data, baselineError))
        {
            applyBaseline(unified, data, baselineCfg.path);
            baselineActive = true;
        }
        else
        {
            unified.notes.push_back("baseline ignored: " + baselineError);
        }
    }

    // With a baseline, only new findings decide the verdict - but a broken
    // checker still fails the run. Recording a baseline is not a verdict.
    int exitCode = report.exit_code;
    if (baselineActive && baselineCfg.update)
    {
        exitCode = hasHardFailure(report) ? 2 : 0;
    }
    else if (baselineActive && baselineCfg.fail_on_new_only)
    {
        exitCode = hasHardFailure(report) ? 2 : 0;
        for (const auto& finding : unified.findings)
        {
            if (finding.baseline != BaselineStatus::New)
            {
                continue;
            }
            if (severityFails(finding.severity, request.config.run.fail_on))
            {
                exitCode = 2;
                break;
            }
        }
    }

    report.exit_code = exitCode;
    unified.exit_code = exitCode;

    std::string writeError;
    if (!writer.writeReport(report, unified, writeError))
    {
        report.exit_code = 1;
        CheckerResult writeFail;
        writeFail.checker_id = "report-writer";
        writeFail.checker_kind = "engine";
        writeFail.status = CheckerStatus::StartError;
        writeFail.message = writeError;
        report.checkers.push_back(std::move(writeFail));
    }

    unifiedOut = std::move(unified);
    return report;
}

int exitCodeFor(const RunReport& report, FailOn failOn)
{
    for (const auto& c : report.checkers)
    {
        if (c.checker_kind == "engine" &&
            (c.status == CheckerStatus::StartError ||
             c.status == CheckerStatus::ToolError))
        {
            return 1;
        }
        if (checkerResultFails(c, failOn))
        {
            return 2;
        }
    }
    return 0;
}

} // namespace cppsentinel
