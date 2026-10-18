// CppSentinel CLI entry point (v2).
//
// Responsibilities: parse `run --config --target`, load/validate config,
// construct Engine with built-in checkers, print console summary, return exit
// code. No tool execution or report-directory logic lives here.
//
// Usage: CppSentinel run --config <file> --target <file-or-dir>

#include "cppsentinel/config/ConfigLoader.h"
#include "cppsentinel/core/CheckerRegistry.h"
#include "cppsentinel/core/Doctor.h"
#include "cppsentinel/core/Engine.h"
#include "cppsentinel/core/ToolProbe.h"
#include "cppsentinel/process/Msys2ProcessRunner.h"
#include "cppsentinel/process/NativeProcessRunner.h"
#include "cppsentinel/report/ConsoleReporter.h"
#include "cppsentinel/report/Findings.h"
#include "cppsentinel/util/FileIo.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

void printUsage(const char* argv0)
{
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " run --config <file> --target <file-or-dir>\n"
        << "  " << argv0 << " doctor --config <file>\n"
        << "  " << argv0 << " init [--dir <dir>]\n"
        << "\n"
        << "Options:\n"
        << "  --config           Path to v2 Configuration.toml\n"
        << "  --target           Source file or directory to check\n"
        << "  --baseline <file>  Compare against a baseline and report new "
           "findings\n"
        << "  --update-baseline  Rewrite the baseline from this run\n"
        << "  --jobs <n>         Run up to n checkers in parallel (1..16)\n"
        << "  --cache            Reuse results for an unchanged project tree\n"
        << "  --json             doctor: emit JSON instead of text\n"
        << "\n"
        << "Environment:\n"
        << "  MSYS2_ROOT   Optional MSYS2 install root\n"
        << "\n"
        << "Example:\n"
        << "  " << argv0
        << " run --config .\\Configuration\\Configuration.toml --target "
           ".\\main.cpp\n";
}

struct CliArgs
{
    std::string command;
    std::filesystem::path config;
    std::filesystem::path target;
    std::filesystem::path baseline;
    bool updateBaseline = false;
    bool json = false;
    bool cache = false;
    int jobs = 0;
    std::filesystem::path initDir;
};

std::optional<CliArgs> parseArgs(int argc, char* argv[], std::string& error)
{
    if (argc < 2)
    {
        error = "missing command";
        return std::nullopt;
    }

    const std::string_view cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help")
    {
        error = "help";
        return std::nullopt;
    }
    if (cmd != "run" && cmd != "doctor" && cmd != "init")
    {
        error = "unknown command '" + std::string(cmd) +
                "' (expected run, doctor or init)";
        return std::nullopt;
    }

    CliArgs args;
    args.command = std::string(cmd);
    for (int i = 2; i < argc; ++i)
    {
        const std::string_view opt = argv[i];
        auto needValue = [&](const char* name) -> std::optional<std::string>
        {
            if (i + 1 >= argc)
            {
                error = std::string(name) + " requires a value";
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };

        if (opt == "--config")
        {
            auto v = needValue("--config");
            if (!v)
            {
                return std::nullopt;
            }
            args.config = *v;
        }
        else if (opt == "--target")
        {
            auto v = needValue("--target");
            if (!v)
            {
                return std::nullopt;
            }
            args.target = *v;
        }
        else if (opt == "--baseline")
        {
            auto v = needValue("--baseline");
            if (!v)
            {
                return std::nullopt;
            }
            args.baseline = *v;
        }
        else if (opt == "--update-baseline")
        {
            args.updateBaseline = true;
        }
        else if (opt == "--jobs")
        {
            auto v = needValue("--jobs");
            if (!v)
            {
                return std::nullopt;
            }
            try
            {
                args.jobs = std::stoi(*v);
            }
            catch (const std::exception&)
            {
                error = "--jobs requires an integer";
                return std::nullopt;
            }
        }
        else if (opt == "--dir")
        {
            auto v = needValue("--dir");
            if (!v)
            {
                return std::nullopt;
            }
            args.initDir = *v;
        }
        else if (opt == "--json")
        {
            args.json = true;
        }
        else if (opt == "--cache")
        {
            args.cache = true;
        }
        else if (opt == "-h" || opt == "--help")
        {
            error = "help";
            return std::nullopt;
        }
        else
        {
            error = "unknown option '" + std::string(opt) + "'";
            return std::nullopt;
        }
    }

    if (args.command == "init")
    {
        return args;
    }

    if (args.config.empty())
    {
        error = "missing required --config";
        return std::nullopt;
    }
    if (args.command == "run" && args.target.empty())
    {
        error = "missing required --target";
        return std::nullopt;
    }
    return args;
}

void printConfigDiagnostics(
    const std::vector<cppsentinel::ConfigDiagnostic>& diags)
{
    std::cerr << "Configuration errors:\n";
    for (const auto& d : diags)
    {
        std::cerr << "  - ";
        if (d.line)
        {
            std::cerr << "line " << *d.line;
            if (d.column)
            {
                std::cerr << ":" << *d.column;
            }
            std::cerr << " ";
        }
        if (!d.path.empty())
        {
            std::cerr << "[" << d.path << "] ";
        }
        std::cerr << d.message;
        if (!d.hint.empty())
        {
            std::cerr << "  (hint: " << d.hint << ')';
        }
        std::cerr << '\n';
    }
}

int runDoctorCommand(const CliArgs& args)
{
    auto loaded = cppsentinel::loadConfig(args.config);
    if (!loaded)
    {
        printConfigDiagnostics(loaded.diagnostics);
        return 1;
    }

    // Real tool probes: build the same runner the run would use, then ask
    // each configured executable for its version.
    const auto projectRoot = loaded.config.project.root;
    cppsentinel::ToolProbe probe =
        [projectRoot](const std::string&,
                      const cppsentinel::EnvironmentProfile& env,
                      const std::string& executable) -> std::string
    {
        cppsentinel::ProcessRunnerPtr runner;
        if (env.kind == cppsentinel::EnvironmentKind::Msys2)
        {
            runner = std::make_shared<cppsentinel::Msys2ProcessRunner>(env);
        }
        else
        {
            runner = std::make_shared<cppsentinel::NativeProcessRunner>();
        }
        return cppsentinel::probeToolVersion(runner, executable, projectRoot,
                                             std::chrono::seconds(15));
    };

    const auto report = cppsentinel::runDoctor(loaded.config, probe);
    std::cout << (args.json ? cppsentinel::formatDoctorJson(report)
                            : cppsentinel::formatDoctorText(report));
    if (args.json)
    {
        std::cout << '\n';
    }
    return report.ok() ? 0 : 1;
}

int runInitCommand(const CliArgs& args)
{
    namespace fs = std::filesystem;
    const fs::path dir =
        args.initDir.empty() ? fs::current_path() : args.initDir;
    const fs::path configPath = dir / "Configuration" / "Configuration.toml";

    std::error_code ec;
    fs::create_directories(configPath.parent_path(), ec);
    if (ec)
    {
        std::cerr << "Error: cannot create "
                  << configPath.parent_path().string() << ": " << ec.message()
                  << '\n';
        return 1;
    }
    if (fs::exists(configPath, ec))
    {
        std::cerr << "Error: " << configPath.string()
                  << " already exists; refusing to overwrite\n";
        return 1;
    }

    static constexpr const char* kTemplate =
        R"(# CppSentinel configuration (schema 2)
schema_version = 2

[project]
root = ".."
compilation_database = "../build/compile_commands.json"
report_dir = "../report"

[run]
max_parallel = 2
timeout_seconds = 120
fail_on = "warning"
# baseline = "../Configuration/baseline.json"
# fail_on_new = true

[filter]
# ignore_rules = ["modernize-*"]
# ignore_paths = ["third_party/**"]
# ignore_messages = ["is too short"]
# min_severity = "warning"
inline_suppressions = true

[environment.native]
kind = "native"
inherit_parent = true

[environment.mingw64]
kind = "msys2"
root = "C:/msys64"
msystem = "MINGW64"
inherit_parent = true

[[checker]]
id = "clang-tidy"
kind = "clang-tidy"
enabled = true
environment = "mingw64"

[[checker]]
id = "clang-format"
kind = "clang-format"
enabled = true
environment = "mingw64"

[[checker]]
id = "compiler-diagnostics"
kind = "compiler-diagnostics"
enabled = true
environment = "mingw64"
compiler = "c++"

[[checker]]
id = "cppcheck"
kind = "cppcheck"
enabled = true
environment = "mingw64"
)";

    std::string writeError;
    if (!cppsentinel::writeTextFile(configPath, kTemplate, writeError))
    {
        std::cerr << "Error: " << writeError << '\n';
        return 1;
    }
    std::cout << "wrote " << configPath.string()
              << "\nnext: CppSentinel doctor --config \"" << configPath.string()
              << "\"\n";
    return 0;
}

int runMain(int argc, char* argv[])
{
    std::string parseError;
    auto args = parseArgs(argc, argv, parseError);
    if (!args)
    {
        if (parseError == "help")
        {
            printUsage(argv[0]);
            return 0;
        }
        std::cerr << "Error: " << parseError << "\n\n";
        printUsage(argv[0]);
        return 1;
    }

    if (args->command == "init")
    {
        return runInitCommand(*args);
    }
    if (args->command == "doctor")
    {
        return runDoctorCommand(*args);
    }

    auto loaded = cppsentinel::loadConfig(args->config);
    if (!loaded)
    {
        printConfigDiagnostics(loaded.diagnostics);
        return 1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(args->target, ec))
    {
        std::cerr << "Error: target path does not exist: " << args->target
                  << '\n';
        return 1;
    }

    cppsentinel::CheckerRegistry registry;
    cppsentinel::registerBuiltinCheckers(registry);
    cppsentinel::Engine engine(std::move(registry));

    cppsentinel::RunRequest request;
    request.config = std::move(loaded.config);

    // CLI overrides applied on top of the configuration file.
    if (!args->baseline.empty())
    {
        request.config.baseline.path =
            std::filesystem::absolute(args->baseline, ec);
    }
    if (args->updateBaseline)
    {
        request.config.baseline.update = true;
    }
    if (args->jobs > 0)
    {
        request.config.run.max_parallel = args->jobs;
    }
    if (args->cache)
    {
        request.config.run.cache = true;
    }

    request.target = std::filesystem::absolute(args->target, ec);
    if (ec)
    {
        request.target = args->target;
    }

    cppsentinel::UnifiedReport unified;
    const auto report = engine.run(request, unified);
    cppsentinel::writeUnifiedConsoleSummary(unified, std::cout);

    if (!report.metadata.report_dir.empty())
    {
        const auto& dir = report.metadata.report_dir;
        std::cout << "\nreport.md       : " << (dir / "report.md").string()
                  << "\nreport.json     : " << (dir / "report.json").string()
                  << "\nreport.sarif    : " << (dir / "report.sarif").string()
                  << "\nreport.raw.json : "
                  << (dir / "report.raw.json").string() << '\n';
    }

    return report.exit_code;
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        return runMain(argc, argv);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unhandled unknown exception\n";
        return 1;
    }
}
