// CppSentinel CLI entry point (v2).
//
// Responsibilities: parse `run --config --target`, load/validate config,
// construct Engine with built-in checkers, print console summary, return exit code.
// No tool execution or report-directory logic lives here.
//
// Usage: CppSentinel run --config <file> --target <file-or-dir>

#include "cppsentinel/config/ConfigLoader.h"
#include "cppsentinel/core/CheckerRegistry.h"
#include "cppsentinel/core/Engine.h"
#include "cppsentinel/report/ConsoleReporter.h"

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
        << "\n"
        << "Options:\n"
        << "  --config   Path to v2 Configuration.toml\n"
        << "  --target   Source file or directory to check\n"
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
    std::filesystem::path config;
    std::filesystem::path target;
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
    if (cmd != "run")
    {
        error = "unknown command '" + std::string(cmd) + "' (expected 'run')";
        return std::nullopt;
    }

    CliArgs args;
    for (int i = 2; i < argc; ++i)
    {
        const std::string_view opt = argv[i];
        auto needValue = [&](const char* name) -> std::optional<std::string> {
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

    if (args.config.empty())
    {
        error = "missing required --config";
        return std::nullopt;
    }
    if (args.target.empty())
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
    request.target = std::filesystem::absolute(args->target, ec);
    if (ec)
    {
        request.target = args->target;
    }

    const auto report = engine.run(request);
    cppsentinel::writeConsoleSummary(report, std::cout);

    if (!report.metadata.report_dir.empty())
    {
        std::cout << "\nreport.json : "
                  << (report.metadata.report_dir / "report.json").string()
                  << '\n';
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
