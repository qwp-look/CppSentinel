// Engine: exclusive report dir, sequential checkers, fail_on exit mapping.

#include "cppsentinel/core/Engine.h"

#include "cppsentinel/core/BuiltinTools.h"
#include "cppsentinel/core/ResultCache.h"
#include "cppsentinel/core/TargetEnumerator.h"
#include "cppsentinel/core/ToolProbe.h"
#include "cppsentinel/process/Msys2ProcessRunner.h"
#include "cppsentinel/process/NativeProcessRunner.h"
#include "cppsentinel/process/PathUtil.h"
#include "cppsentinel/report/Baseline.h"
#include "cppsentinel/report/Filter.h"
#include "cppsentinel/report/FixParser.h"
#include "cppsentinel/report/ReportWriter.h"
#include "cppsentinel/report/SourceContext.h"
#include "cppsentinel/util/FileIo.h"

#include <algorithm>
#include <future>
#include <map>
#include <set>
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

RunReport Engine::run(const RunRequest& request,
                      UnifiedReport& unifiedOut) const
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

    // Expand the target once so every checker analyses the same file set.
    const auto targets =
        enumerateTargets(request.target, request.config.project.root,
                         request.config.filter.ignore_paths);
    std::vector<std::string> runNotes;
    if (targets.excluded > 0)
    {
        runNotes.push_back(std::to_string(targets.excluded) +
                           " file(s) excluded by filter.ignore_paths");
    }
    if (targets.truncated)
    {
        runNotes.push_back(
            "target enumeration stopped early (unreadable directory)");
    }

    struct PendingJob
    {
        std::size_t index = 0;
        CheckerPtr checker;
        CheckerContext context;
    };

    const auto& checkerConfigs = request.config.checkers;
    std::vector<CheckerResult> results(checkerConfigs.size());
    std::vector<PendingJob> jobs;
    jobs.reserve(checkerConfigs.size());
    std::map<std::string, std::string> versionByKey;
    std::map<std::size_t, std::string> cacheKeys;
    ResultCache cache(request.config.run.cache_dir, request.config.run.cache);
    const std::string fingerprint =
        cache.enabled() ? projectFingerprint(request.config.project.root)
                        : std::string{};

    // Probe each distinct (environment, executable) once, in parallel:
    // launching an MSYS2 login shell per probe is the dominant cost of a cached
    // run.
    struct ProbeJob
    {
        std::string key;
        std::string environment;
        std::string executable;
        std::string checkerId;
    };

    {
        std::vector<ProbeJob> probeJobs;
        std::set<std::string> seen;
        for (const auto& checkerCfg : request.config.checkers)
        {
            if (!checkerCfg.enabled ||
                request.config.environments.find(checkerCfg.environment) ==
                    request.config.environments.end())
            {
                continue;
            }
            std::string executable = defaultExecutableForKind(checkerCfg.kind);
            if (const auto it = checkerCfg.options.find("executable");
                it != checkerCfg.options.end() && !it->second.empty())
            {
                executable = it->second;
            }
            if (executable.empty())
            {
                continue;
            }
            const std::string key = checkerCfg.environment + "|" + executable;
            if (!seen.insert(key).second)
            {
                continue;
            }
            probeJobs.push_back(
                {key, checkerCfg.environment, executable, checkerCfg.id});
        }

        std::vector<std::future<std::string>> probes;
        probes.reserve(probeJobs.size());
        for (const auto& probeJob : probeJobs)
        {
            const auto& env =
                request.config.environments.at(probeJob.environment);
            const auto runner =
                runnerForEnvironment(env, request.default_runner);
            const auto root = request.config.project.root;
            const auto executable = probeJob.executable;
            probes.push_back(std::async(std::launch::async,
                                        [runner, executable, root]()
                                        {
                                            return probeToolVersion(
                                                runner, executable, root,
                                                std::chrono::seconds(15));
                                        }));
        }

        for (std::size_t i = 0; i < probeJobs.size(); ++i)
        {
            std::string version;
            try
            {
                version = probes[i].get();
            }
            catch (const std::exception&)
            {
                version.clear();
            }
            versionByKey[probeJobs[i].key] = version;
            if (!version.empty())
            {
                ToolVersion info;
                info.checker_id = probeJobs[i].checkerId;
                info.executable = probeJobs[i].executable;
                info.version = version;
                report.metadata.tool_versions.push_back(std::move(info));
            }
        }
    }

    for (std::size_t i = 0; i < checkerConfigs.size(); ++i)
    {
        const auto& checkerCfg = checkerConfigs[i];
        if (!checkerCfg.enabled)
        {
            results[i] = makeSkipped(checkerCfg, "disabled in configuration");
            continue;
        }

        const auto envIt =
            request.config.environments.find(checkerCfg.environment);
        if (envIt == request.config.environments.end())
        {
            results[i] =
                makeConfigError(checkerCfg, "unknown environment '" +
                                                checkerCfg.environment + "'");
            continue;
        }

        auto checker = registry_.create(checkerCfg);
        if (!checker)
        {
            results[i] = makeConfigError(checkerCfg, "unknown checker kind '" +
                                                         checkerCfg.kind + "'");
            continue;
        }

        std::error_code ec;
        const auto artifactDir = writer.checkerArtifactDir(checkerCfg.id);
        std::filesystem::create_directories(artifactDir, ec);

        PendingJob job;
        job.index = i;
        job.checker = std::move(checker);

        auto& context = job.context;
        context.config = &request.config;
        context.checker = &checkerCfg;
        context.environment = &envIt->second;
        context.runner =
            runnerForEnvironment(envIt->second, request.default_runner);
        context.target = request.target;
        context.files = targets.files;
        context.project_root = request.config.project.root;
        context.report_dir = layout->root;
        context.logs_dir = layout->logs_dir;
        context.artifacts_dir = artifactDir;
        context.compilation_database =
            request.config.project.compilation_database;
        context.timeout = checkerCfg.timeout.count() > 0
                              ? checkerCfg.timeout
                              : request.config.run.timeout_seconds;

        std::string executable = defaultExecutableForKind(checkerCfg.kind);
        if (const auto it = checkerCfg.options.find("executable");
            it != checkerCfg.options.end() && !it->second.empty())
        {
            executable = it->second;
        }
        const std::string versionKey =
            checkerCfg.environment + "|" + executable;

        if (cache.enabled())
        {
            const auto versionIt = versionByKey.find(versionKey);
            const std::string versionText = versionIt == versionByKey.end()
                                                ? std::string{}
                                                : versionIt->second;
            const std::string key =
                cache.keyFor(checkerCfg, versionText, fingerprint);
            if (auto cached = cache.load(key))
            {
                cached->checker_id = checkerCfg.id;
                cached->checker_kind = checkerCfg.kind;
                cached->message +=
                    cached->message.empty() ? "cached" : " (cached)";
                results[i] = std::move(*cached);
                continue;
            }
            cacheKeys[i] = key;
        }

        jobs.push_back(std::move(job));
    }

    // Run checkers in waves of `max_parallel`; results keep declaration order.
    const auto parallel = static_cast<std::size_t>(
        std::max(1, std::min(request.config.run.max_parallel, kMaxParallel)));

    for (std::size_t start = 0; start < jobs.size(); start += parallel)
    {
        const std::size_t end = std::min(jobs.size(), start + parallel);
        std::vector<std::future<CheckerResult>> wave;
        wave.reserve(end - start);
        for (std::size_t k = start; k < end; ++k)
        {
            wave.push_back(
                std::async(std::launch::async, [&jobs, k]()
                           { return jobs[k].checker->run(jobs[k].context); }));
        }
        for (std::size_t k = start; k < end; ++k)
        {
            CheckerResult result;
            try
            {
                result = wave[k - start].get();
            }
            catch (const std::exception& ex)
            {
                result.checker_id = jobs[k].context.checker->id;
                result.checker_kind = jobs[k].context.checker->kind;
                result.status = CheckerStatus::ToolError;
                result.message = std::string("checker threw: ") + ex.what();
            }
            results[jobs[k].index] = std::move(result);
        }
    }

    for (const auto& [index, key] : cacheKeys)
    {
        cache.store(key, results[index]);
    }

    for (std::size_t i = 0; i < checkerConfigs.size(); ++i)
    {
        auto& result = results[i];
        const auto& checkerCfg = checkerConfigs[i];
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

    // Normalize into the unified finding layer once; every artifact derives
    // from it.
    NormalizeOptions normalize;
    normalize.project_root = request.config.project.root;
    auto unified = buildUnifiedReport(report, normalize);
    unified.notes.insert(unified.notes.begin(), runNotes.begin(),
                         runNotes.end());

    // Filters run before baseline matching: a suppressed finding is not
    // "known", it is simply not reported.
    applyFilters(unified, request.config.filter, request.config.project.root);

    // Enrich what survived filtering with source context and tool-provided
    // fixes.
    attachSnippets(unified, request.config.project.root);
    for (const auto& checker : report.checkers)
    {
        for (const auto& artifact : checker.artifacts)
        {
            const std::string artifactName = artifact.filename().string();
            if (artifactName.rfind("clang-tidy-fixes", 0) != 0 ||
                artifact.extension() != ".yaml")
            {
                continue;
            }
            std::string readError;
            if (auto text = readTextFile(artifact, readError))
            {
                attachFixes(unified, *text, request.config.project.root);
            }
        }
    }

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
            unified.notes.push_back(
                "baseline updated: " + baselineCfg.path.string() + " (" +
                std::to_string(data.findings.size()) + " findings recorded)");
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
    // Verdict from what is actually reported: suppressed findings and findings
    // removed by filters must not fail the run. Hard checker failures always
    // do.
    int exitCode = 0;
    if (hasHardFailure(report))
    {
        exitCode = 2;
    }
    else
    {
        for (const auto& checker : report.checkers)
        {
            if (checker.status == CheckerStatus::Failed)
            {
                exitCode = 2;
                break;
            }
        }
        if (exitCode == 0)
        {
            for (const auto& finding : unified.findings)
            {
                if (severityFails(finding.severity, request.config.run.fail_on))
                {
                    exitCode = 2;
                    break;
                }
            }
        }
    }

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
