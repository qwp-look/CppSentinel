// Built-in checkers: clang-tidy, clang-format, compiler-diagnostics, cppcheck.
// Defaults live here; Configuration.toml only supplies options / extra_args.
//
// Every checker works from CheckerContext::files (the expanded target) and
// invokes the tool in batches, so all checkers see exactly the same files.

#include "cppsentinel/checker/BuiltinCheckers.h"

#include "cppsentinel/checker/CompileCommands.h"
#include "cppsentinel/checker/DiagnosticParser.h"
#include "cppsentinel/process/PathUtil.h"
#include "cppsentinel/util/FileIo.h"

#include <chrono>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace cppsentinel
{
namespace
{

/// Keep argv well below the Windows command-line limit.
constexpr std::size_t kFilesPerInvocation = 40;

std::string optionOr(const CheckerConfig& cfg, const std::string& key,
                     std::string fallback)
{
    const auto it = cfg.options.find(key);
    if (it == cfg.options.end() || it->second.empty())
    {
        return fallback;
    }
    return it->second;
}

std::vector<std::filesystem::path> targetFiles(const CheckerContext& context)
{
    if (!context.files.empty())
    {
        return context.files;
    }
    return {context.target};
}

ProcessRequest baseRequest(const CheckerContext& context,
                           const std::string& executable,
                           std::vector<std::string> args,
                           const std::string& batchSuffix = {})
{
    ProcessRequest req;
    req.executable = resolveExecutablePath(executable);
    req.arguments = std::move(args);
    req.working_directory = context.project_root;
    req.timeout = context.timeout;

    std::string token = sanitizeFileToken(context.checker->id);
    if (!batchSuffix.empty())
    {
        token += "-" + batchSuffix;
    }
    req.stdout_log = context.logs_dir / (token + ".stdout.log");
    req.stderr_log = context.logs_dir / (token + ".stderr.log");
    req.inherit_parent_env = true;
    return req;
}

CheckerResult emptyResult(const CheckerContext& context)
{
    CheckerResult result;
    result.checker_id = context.checker->id;
    result.checker_kind = context.checker->kind;
    result.status = CheckerStatus::Succeeded;
    return result;
}

int statusRank(CheckerStatus status) noexcept
{
    switch (status)
    {
    case CheckerStatus::Succeeded:
        return 0;
    case CheckerStatus::Skipped:
        return 1;
    case CheckerStatus::Failed:
        return 2;
    case CheckerStatus::ConfigError:
        return 3;
    case CheckerStatus::ToolError:
        return 4;
    case CheckerStatus::StartError:
        return 5;
    case CheckerStatus::TimedOut:
        return 6;
    }
    return 0;
}

/// Fold one batch result into the checker-wide result.
void mergeResult(CheckerResult& into, CheckerResult part)
{
    into.duration += part.duration;
    if (into.stdout_log.empty())
    {
        into.stdout_log = part.stdout_log;
        into.stderr_log = part.stderr_log;
    }
    into.diagnostics.insert(into.diagnostics.end(),
                            std::make_move_iterator(part.diagnostics.begin()),
                            std::make_move_iterator(part.diagnostics.end()));
    into.artifacts.insert(into.artifacts.end(),
                          std::make_move_iterator(part.artifacts.begin()),
                          std::make_move_iterator(part.artifacts.end()));
    if (statusRank(part.status) > statusRank(into.status))
    {
        into.status = part.status;
    }
    if (part.exit_code && *part.exit_code != 0 && !into.exit_code)
    {
        into.exit_code = part.exit_code;
    }
    if (!part.message.empty())
    {
        into.message = std::move(part.message);
    }
}

std::vector<std::vector<std::filesystem::path>>
chunkFiles(const std::vector<std::filesystem::path>& files, std::size_t size)
{
    std::vector<std::vector<std::filesystem::path>> batches;
    for (std::size_t start = 0; start < files.size(); start += size)
    {
        const std::size_t end = start + size < files.size() ? start + size
                                                            : files.size();
        batches.emplace_back(files.begin() + static_cast<std::ptrdiff_t>(start),
                             files.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return batches;
}

std::string batchSummary(std::size_t files, std::size_t batches)
{
    return std::to_string(files) + " file(s) in " + std::to_string(batches) +
           " batch(es)";
}

CheckerResult fromProcess(const CheckerContext& context,
                          const ProcessResult& proc,
                          std::vector<Diagnostic> diagnostics,
                          std::vector<std::filesystem::path> artifacts = {})
{
    CheckerResult result;
    result.checker_id = context.checker->id;
    result.checker_kind = context.checker->kind;
    result.exit_code = proc.exit_code;
    result.duration = proc.duration;
    result.stdout_log = proc.stdout_log;
    result.stderr_log = proc.stderr_log;
    result.diagnostics = std::move(diagnostics);
    result.artifacts = std::move(artifacts);
    result.message = proc.error_message;

    // Soft tool exits (non-zero with diagnostics) stay Succeeded so [run].fail_on
    // decides policy from Diagnostic severities. Hard failures remain sticky.
    switch (proc.state)
    {
    case ProcessState::Succeeded:
        result.status = CheckerStatus::Succeeded;
        break;
    case ProcessState::Failed:
        if (result.diagnostics.empty())
        {
            result.status = CheckerStatus::ToolError;
            if (result.message.empty())
            {
                result.message = "tool exited non-zero without parseable diagnostics";
            }
        }
        else
        {
            result.status = CheckerStatus::Succeeded;
        }
        break;
    case ProcessState::TimedOut:
        result.status = CheckerStatus::TimedOut;
        break;
    case ProcessState::StartError:
        result.status = CheckerStatus::StartError;
        break;
    }
    return result;
}

bool compilationDatabaseReady(const CheckerContext& context, std::string& error)
{
    if (context.compilation_database.empty())
    {
        error = "project.compilation_database is not set";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(context.compilation_database, ec) ||
        ec)
    {
        error = "compilation database not found: " +
                context.compilation_database.string();
        return false;
    }
    return true;
}

std::vector<Diagnostic> parseLogs(const ProcessResult& proc)
{
    std::vector<Diagnostic> diags;
    std::string err;
    if (auto out = readTextFile(proc.stdout_log, err))
    {
        auto parsed = parseClangStyleDiagnostics(*out);
        diags.insert(diags.end(), parsed.begin(), parsed.end());
    }
    if (auto errText = readTextFile(proc.stderr_log, err))
    {
        auto parsed = parseClangStyleDiagnostics(*errText);
        diags.insert(diags.end(), parsed.begin(), parsed.end());
    }
    return diags;
}

class ClangTidyChecker final : public Checker
{
public:
    explicit ClangTidyChecker(CheckerConfig config) : config_(std::move(config)) {}

    std::string_view kind() const noexcept override
    {
        return "clang-tidy";
    }

    CheckerResult run(const CheckerContext& context) const override
    {
        std::string cdbError;
        if (!compilationDatabaseReady(context, cdbError))
        {
            CheckerResult result = emptyResult(context);
            result.status = CheckerStatus::ConfigError;
            result.message = cdbError;
            return result;
        }

        const std::string exe = optionOr(config_, "executable", "clang-tidy");
        const auto files = targetFiles(context);
        const auto batches = chunkFiles(files, kFilesPerInvocation);

        // -p points at the directory containing compile_commands.json
        const auto p = context.compilation_database.parent_path().empty()
                           ? context.compilation_database
                           : context.compilation_database.parent_path();

        auto total = emptyResult(context);
        std::size_t index = 0;

        for (const auto& batch : batches)
        {
            std::vector<std::string> args;
            args.emplace_back("-p");
            args.push_back(p.string());

            const auto checks = optionOr(
                config_, "checks",
                "clang-diagnostic-*,clang-analyzer-*,bugprone-*,cert-*,"
                "performance-*,portability-*,readability-*,modernize-*");
            args.emplace_back("-checks=" + checks);

            const auto headerFilter = optionOr(config_, "header_filter", ".*");
            args.emplace_back("-header-filter=" + headerFilter);
            args.emplace_back("-warnings-as-errors=");
            args.emplace_back("--format-style=file");
            args.emplace_back("-quiet");

            const auto fixesPath =
                context.artifacts_dir /
                ("clang-tidy-fixes-" + std::to_string(index) + ".yaml");
            args.emplace_back("--export-fixes=" + fixesPath.string());

            for (const auto& extra : config_.extra_args)
            {
                args.push_back(extra);
            }
            for (const auto& file : batch)
            {
                args.push_back(file.string());
            }

            auto req = baseRequest(context, exe, std::move(args),
                                   std::to_string(index));
            const auto proc = context.runner->run(req);
            auto diags = parseLogs(proc);

            std::vector<std::filesystem::path> artifacts;
            std::error_code ec;
            if (std::filesystem::exists(fixesPath, ec))
            {
                artifacts.push_back(fixesPath);
            }

            auto part = fromProcess(context, proc, std::move(diags),
                                    std::move(artifacts));
            mergeResult(total, std::move(part));
            ++index;
        }

        if (total.status == CheckerStatus::Succeeded)
        {
            total.message = total.diagnostics.empty()
                                ? "clang-tidy clean (" +
                                      batchSummary(files.size(), batches.size()) + ")"
                                : "clang-tidy reported " +
                                      std::to_string(total.diagnostics.size()) +
                                      " diagnostics";
        }
        return total;
    }

private:
    CheckerConfig config_;
};

class ClangFormatChecker final : public Checker
{
public:
    explicit ClangFormatChecker(CheckerConfig config) : config_(std::move(config))
    {
    }

    std::string_view kind() const noexcept override
    {
        return "clang-format";
    }

    CheckerResult run(const CheckerContext& context) const override
    {
        const std::string exe = optionOr(config_, "executable", "clang-format");
        const auto style = optionOr(config_, "style", "file");
        const auto files = targetFiles(context);

        auto total = emptyResult(context);
        std::size_t index = 0;

        // One file per invocation: a non-zero exit must be attributable to a file.
        for (const auto& file : files)
        {
            const auto replacements = context.artifacts_dir /
                                      ("replacements-" + std::to_string(index) +
                                       ".xml");

            std::vector<std::string> args = {
                "--dry-run",
                "--Werror",
                "--style=" + style,
                "--output-replacements-xml",
                file.string(),
            };
            for (const auto& extra : config_.extra_args)
            {
                args.push_back(extra);
            }

            auto req = baseRequest(context, exe, std::move(args),
                                   std::to_string(index));
            const auto proc = context.runner->run(req);

            std::vector<Diagnostic> diags;
            std::string err;
            std::string combined;
            if (auto out = readTextFile(proc.stdout_log, err))
            {
                combined += *out;
                std::string writeErr;
                writeTextFile(replacements, *out, writeErr);
            }
            if (auto errText = readTextFile(proc.stderr_log, err))
            {
                combined += *errText;
            }

            if (proc.state == ProcessState::Failed ||
                (proc.exit_code && *proc.exit_code != 0))
            {
                Diagnostic d;
                d.file = file;
                d.severity = Severity::Style;
                d.rule = "clang-format";
                d.message = "file is not compliant with clang-format style";
                if (!combined.empty())
                {
                    d.message += " (see replacements XML / logs)";
                }
                diags.push_back(std::move(d));
            }

            std::vector<std::filesystem::path> artifacts;
            std::error_code ec;
            if (std::filesystem::exists(replacements, ec))
            {
                artifacts.push_back(replacements);
            }

            auto part = fromProcess(context, proc, std::move(diags),
                                    std::move(artifacts));
            mergeResult(total, std::move(part));
            ++index;
        }

        if (total.status == CheckerStatus::Succeeded)
        {
            total.message = total.diagnostics.empty()
                                ? "clang-format clean"
                                : "clang-format reported style issues";
        }
        return total;
    }

private:
    CheckerConfig config_;
};

class CompilerDiagnosticsChecker final : public Checker
{
public:
    explicit CompilerDiagnosticsChecker(CheckerConfig config)
        : config_(std::move(config))
    {
    }

    std::string_view kind() const noexcept override
    {
        return "compiler-diagnostics";
    }

    CheckerResult run(const CheckerContext& context) const override
    {
        std::string cdbError;
        if (!compilationDatabaseReady(context, cdbError))
        {
            CheckerResult result = emptyResult(context);
            result.status = CheckerStatus::ConfigError;
            result.message = cdbError;
            return result;
        }

        std::string loadErr;
        const auto entries =
            loadCompileCommands(context.compilation_database, loadErr);
        const auto files = targetFiles(context);

        const std::string defaultCompiler =
            optionOr(config_, "compiler", "c++");

        auto total = emptyResult(context);
        std::size_t missing = 0;
        std::size_t checked = 0;

        for (const auto& file : files)
        {
            const auto found = findCompileCommand(entries, file);
            if (!found)
            {
                ++missing;
                continue;
            }

            std::filesystem::path cwd = context.project_root;
            if (!found->directory.empty())
            {
                cwd = found->directory;
            }

            std::string compiler = defaultCompiler;
            auto argv = found->arguments;
            if (argv.empty() && !found->command.empty())
            {
                argv = splitCommandLine(found->command);
            }
            if (!argv.empty())
            {
                compiler = argv.front();
            }

            auto args = syntaxOnlyArgsFromCompileCommand(*found, file);
            for (const auto& extra : config_.extra_args)
            {
                args.push_back(extra);
            }

            auto req = baseRequest(context, compiler, std::move(args),
                                   std::to_string(checked));
            req.working_directory = cwd;
            const auto proc = context.runner->run(req);
            auto diags = parseLogs(proc);

            if (proc.state == ProcessState::Failed && diags.empty())
            {
                Diagnostic d;
                d.file = file;
                d.severity = Severity::Error;
                d.rule = "compiler";
                d.message = proc.error_message.empty()
                                ? "compiler returned non-zero exit code"
                                : proc.error_message;
                diags.push_back(std::move(d));
            }

            mergeResult(total, fromProcess(context, proc, std::move(diags)));
            ++checked;
        }

        if (checked == 0)
        {
            CheckerResult result = emptyResult(context);
            result.status = CheckerStatus::ConfigError;
            result.message =
                "no compile_commands entry for the target file(s)" +
                (loadErr.empty() ? "" : " (" + loadErr + ")");
            return result;
        }

        if (total.status == CheckerStatus::Succeeded)
        {
            total.message = total.diagnostics.empty()
                                ? "compiler diagnostics clean"
                                : "compiler reported diagnostics";
        }
        if (missing > 0)
        {
            total.message += " (" + std::to_string(missing) +
                             " file(s) had no compile_commands entry)";
        }
        return total;
    }

private:
    CheckerConfig config_;
};

class CppcheckChecker final : public Checker
{
public:
    explicit CppcheckChecker(CheckerConfig config) : config_(std::move(config)) {}

    std::string_view kind() const noexcept override
    {
        return "cppcheck";
    }

    CheckerResult run(const CheckerContext& context) const override
    {
        const std::string exe = optionOr(config_, "executable", "cppcheck");
        const auto enable = optionOr(
            config_, "enable",
            "warning,style,performance,portability,information");
        const auto stdv = optionOr(config_, "std", "c++20");

        const auto files = targetFiles(context);
        const auto batches = chunkFiles(files, kFilesPerInvocation);

        auto total = emptyResult(context);
        std::size_t index = 0;

        for (const auto& batch : batches)
        {
            std::vector<std::string> args = {
                "--enable=" + enable,
                "--std=" + stdv,
                "--language=c++",
                "--force",
                "--inline-suppr",
                "--suppress=missingIncludeSystem",
                "--template={file}:{line}:{column}: {severity}: {message} [{id}]",
                "-I",
                (context.project_root / "include").string(),
                "-I",
                context.project_root.string(),
            };

            if (auto it = config_.options.find("suppressions");
                it != config_.options.end() && !it->second.empty())
            {
                args.emplace_back("--suppressions-list=" + it->second);
            }
            for (const auto& extra : config_.extra_args)
            {
                args.push_back(extra);
            }
            for (const auto& file : batch)
            {
                args.push_back(file.string());
            }

            auto req = baseRequest(context, exe, std::move(args),
                                   std::to_string(index));
            const auto proc = context.runner->run(req);

            std::vector<Diagnostic> diags;
            std::string err;
            if (auto out = readTextFile(proc.stdout_log, err))
            {
                auto parsed = parseCppcheckDiagnostics(*out);
                diags.insert(diags.end(), parsed.begin(), parsed.end());
            }
            if (auto errText = readTextFile(proc.stderr_log, err))
            {
                auto parsed = parseCppcheckDiagnostics(*errText);
                diags.insert(diags.end(), parsed.begin(), parsed.end());
            }

            auto part = fromProcess(context, proc, std::move(diags));
            mergeResult(total, std::move(part));
            ++index;
        }

        if (total.status == CheckerStatus::Succeeded)
        {
            total.message = total.diagnostics.empty()
                                ? "cppcheck clean (" +
                                      batchSummary(files.size(), batches.size()) + ")"
                                : "cppcheck reported issues";
        }
        return total;
    }

private:
    CheckerConfig config_;
};

} // namespace

CheckerPtr createClangTidyChecker(const CheckerConfig& config)
{
    return std::make_unique<ClangTidyChecker>(config);
}

CheckerPtr createClangFormatChecker(const CheckerConfig& config)
{
    return std::make_unique<ClangFormatChecker>(config);
}

CheckerPtr createCompilerDiagnosticsChecker(const CheckerConfig& config)
{
    return std::make_unique<CompilerDiagnosticsChecker>(config);
}

CheckerPtr createCppcheckChecker(const CheckerConfig& config)
{
    return std::make_unique<CppcheckChecker>(config);
}

} // namespace cppsentinel
