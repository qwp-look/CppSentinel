// Built-in checkers: clang-tidy, clang-format, compiler-diagnostics, cppcheck.
// Defaults live here; Configuration.toml only supplies options / extra_args.

#include "cppsentinel/checker/BuiltinCheckers.h"

#include "cppsentinel/checker/CompileCommands.h"
#include "cppsentinel/checker/DiagnosticParser.h"
#include "cppsentinel/process/PathUtil.h"
#include "cppsentinel/util/FileIo.h"

#include <chrono>
#include <utility>

namespace cppsentinel
{
namespace
{

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

ProcessRequest baseRequest(const CheckerContext& context,
                           const std::string& executable,
                           std::vector<std::string> args)
{
    ProcessRequest req;
    req.executable = executable;
    req.arguments = std::move(args);
    req.working_directory = context.project_root;
    req.timeout = context.timeout;
    req.stdout_log =
        context.logs_dir / (sanitizeFileToken(context.checker->id) + ".stdout.log");
    req.stderr_log =
        context.logs_dir / (sanitizeFileToken(context.checker->id) + ".stderr.log");
    req.inherit_parent_env = true;
    return req;
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
        const std::string exe =
            optionOr(config_, "executable", "clang-tidy");

        std::string cdbError;
        if (!compilationDatabaseReady(context, cdbError))
        {
            CheckerResult result;
            result.checker_id = context.checker->id;
            result.checker_kind = kind();
            result.status = CheckerStatus::ConfigError;
            result.message = cdbError;
            result.stdout_log =
                context.logs_dir /
                (sanitizeFileToken(context.checker->id) + ".stdout.log");
            result.stderr_log =
                context.logs_dir /
                (sanitizeFileToken(context.checker->id) + ".stderr.log");
            return result;
        }

        std::vector<std::string> args;
        // -p points at the directory containing compile_commands.json
        const auto p = context.compilation_database.parent_path().empty()
                           ? context.compilation_database
                           : context.compilation_database.parent_path();
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
            context.artifacts_dir / "clang-tidy-fixes.yaml";
        args.emplace_back("--export-fixes=" + fixesPath.string());

        for (const auto& extra : config_.extra_args)
        {
            args.push_back(extra);
        }
        args.push_back(context.target.string());

        auto req = baseRequest(context, exe, std::move(args));
        const auto proc = context.runner->run(req);
        auto diags = parseLogs(proc);

        std::vector<std::filesystem::path> artifacts;
        std::error_code ec;
        if (std::filesystem::exists(fixesPath, ec))
        {
            artifacts.push_back(fixesPath);
        }

        auto result = fromProcess(context, proc, std::move(diags),
                                  std::move(artifacts));
        if (!result.diagnostics.empty() &&
            result.status == CheckerStatus::Succeeded)
        {
            result.message = "clang-tidy reported diagnostics";
        }
        else if (result.status == CheckerStatus::Succeeded)
        {
            result.message = "clang-tidy clean";
        }
        return result;
    }

private:
    CheckerConfig config_;
};

class ClangFormatChecker final : public Checker
{
public:
    explicit ClangFormatChecker(CheckerConfig config)
        : config_(std::move(config))
    {
    }

    std::string_view kind() const noexcept override
    {
        return "clang-format";
    }

    CheckerResult run(const CheckerContext& context) const override
    {
        const std::string exe =
            optionOr(config_, "executable", "clang-format");
        const auto style = optionOr(config_, "style", "file");
        const auto replacements =
            context.artifacts_dir / "replacements.xml";

        std::vector<std::string> args = {
            "--dry-run",
            "--Werror",
            "--style=" + style,
            "--output-replacements-xml",
            context.target.string(),
        };
        for (const auto& extra : config_.extra_args)
        {
            args.push_back(extra);
        }

        auto req = baseRequest(context, exe, std::move(args));
        // Capture XML on stdout into artifact as well by reading log after.
        const auto proc = context.runner->run(req);

        std::vector<Diagnostic> diags;
        std::string err;
        std::string combined;
        if (auto out = readTextFile(proc.stdout_log, err))
        {
            combined += *out;
            // Persist replacements XML artifact from stdout.
            std::string writeErr;
            if (writeTextFile(replacements, *out, writeErr))
            {
                // ok
            }
        }
        if (auto errText = readTextFile(proc.stderr_log, err))
        {
            combined += *errText;
        }

        if (proc.state == ProcessState::Failed ||
            (proc.exit_code && *proc.exit_code != 0))
        {
            Diagnostic d;
            d.file = context.target;
            d.severity = Severity::Style;
            d.rule = "clang-format";
            d.message = "file is not compliant with clang-format style";
            if (!combined.empty())
            {
                // Keep message short; full XML stays in logs/artifacts.
                d.message += " (see replacements.xml / logs)";
            }
            diags.push_back(std::move(d));
        }

        std::vector<std::filesystem::path> artifacts;
        std::error_code ec;
        if (std::filesystem::exists(replacements, ec))
        {
            artifacts.push_back(replacements);
        }

        auto result =
            fromProcess(context, proc, std::move(diags), std::move(artifacts));
        if (!result.diagnostics.empty() &&
            result.status == CheckerStatus::Succeeded)
        {
            result.message = "clang-format reported style issues";
        }
        else if (result.status == CheckerStatus::Succeeded)
        {
            result.message = "clang-format clean";
        }
        return result;
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
        const std::string defaultCompiler =
            optionOr(config_, "compiler", "c++");

        std::string cdbError;
        if (!compilationDatabaseReady(context, cdbError))
        {
            CheckerResult result;
            result.checker_id = context.checker->id;
            result.checker_kind = kind();
            result.status = CheckerStatus::ConfigError;
            result.message = cdbError;
            result.stdout_log =
                context.logs_dir /
                (sanitizeFileToken(context.checker->id) + ".stdout.log");
            result.stderr_log =
                context.logs_dir /
                (sanitizeFileToken(context.checker->id) + ".stderr.log");
            return result;
        }

        std::vector<std::string> args;
        std::filesystem::path cwd = context.project_root;
        std::string compiler = defaultCompiler;

        std::string loadErr;
        auto entries =
            loadCompileCommands(context.compilation_database, loadErr);
        auto found = findCompileCommand(entries, context.target);
        if (!found)
        {
            CheckerResult result;
            result.checker_id = context.checker->id;
            result.checker_kind = kind();
            result.status = CheckerStatus::ConfigError;
            result.message =
                "no compile_commands entry for target: " + context.target.string() +
                (loadErr.empty() ? "" : (" (" + loadErr + ")"));
            return result;
        }

        if (!found->directory.empty())
        {
            cwd = found->directory;
        }
        auto argv = found->arguments;
        if (argv.empty() && !found->command.empty())
        {
            argv = splitCommandLine(found->command);
        }
        if (!argv.empty())
        {
            compiler = argv.front();
        }
        args = syntaxOnlyArgsFromCompileCommand(*found, context.target);

        for (const auto& extra : config_.extra_args)
        {
            args.push_back(extra);
        }

        auto req = baseRequest(context, compiler, std::move(args));
        req.working_directory = cwd;
        const auto proc = context.runner->run(req);
        auto diags = parseLogs(proc);
        auto result = fromProcess(context, proc, std::move(diags));
        if (proc.state == ProcessState::Failed && result.diagnostics.empty())
        {
            Diagnostic d;
            d.file = context.target;
            d.severity = Severity::Error;
            d.rule = "compiler";
            d.message = proc.error_message.empty()
                            ? "compiler returned non-zero exit code"
                            : proc.error_message;
            result.diagnostics.push_back(std::move(d));
            if (result.status == CheckerStatus::ToolError)
            {
                result.status = CheckerStatus::Succeeded;
            }
        }
        if (result.status == CheckerStatus::Succeeded &&
            result.diagnostics.empty())
        {
            result.message = "compiler diagnostics clean";
        }
        else if (result.status == CheckerStatus::Succeeded)
        {
            result.message = "compiler reported diagnostics";
        }
        return result;
    }

private:
    CheckerConfig config_;
};

class CppcheckChecker final : public Checker
{
public:
    explicit CppcheckChecker(CheckerConfig config) : config_(std::move(config))
    {
    }

    std::string_view kind() const noexcept override
    {
        return "cppcheck";
    }

    CheckerResult run(const CheckerContext& context) const override
    {
        const std::string exe =
            optionOr(config_, "executable", "cppcheck");
        const auto enable = optionOr(
            config_, "enable",
            "warning,style,performance,portability,information");
        const auto stdv = optionOr(config_, "std", "c++20");

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
        args.push_back(context.target.string());

        auto req = baseRequest(context, exe, std::move(args));
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

        auto result = fromProcess(context, proc, std::move(diags));
        if (proc.state == ProcessState::Succeeded)
        {
            result.message = "cppcheck clean";
        }
        else if (proc.state == ProcessState::Failed)
        {
            result.message = "cppcheck reported issues";
        }
        return result;
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
