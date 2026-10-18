// Read-only environment checks behind `CppSentinel doctor`.

#include "cppsentinel/core/Doctor.h"

#include "cppsentinel/core/BuiltinTools.h"

#include "cppsentinel/process/Msys2ProcessRunner.h"
#include "cppsentinel/report/Baseline.h"
#include "cppsentinel/report/JsonUtil.h"
#include "cppsentinel/util/FileIo.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;

bool directoryWritable(const fs::path& directory, std::string& detail)
{
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec)
    {
        detail = "cannot create report directory: " + ec.message();
        return false;
    }
    const fs::path probe = directory / ".cppsentinel_write_probe";
    {
        std::string error;
        if (!writeTextFile(probe, "ok", error))
        {
            detail = error;
            return false;
        }
    }
    fs::remove(probe, ec);
    return true;
}

std::string jsonString(std::string_view value)
{
    return '"' + json::escape(value) + '"';
}

} // namespace

bool DoctorReport::ok() const noexcept
{
    for (const auto& check : checks)
    {
        if (!check.ok)
        {
            return false;
        }
    }
    return true;
}

bool DoctorReport::hasWarnings() const noexcept
{
    for (const auto& check : checks)
    {
        if (check.ok && check.warning)
        {
            return true;
        }
    }
    return false;
}

DoctorReport runDoctor(const AppConfig& config, const ToolProbe& probe)
{
    DoctorReport report;
    auto add = [&report](std::string name, bool ok, std::string detail,
                         std::string hint = {}, bool warning = false)
    {
        DoctorCheck check;
        check.name = std::move(name);
        check.ok = ok;
        check.detail = std::move(detail);
        check.hint = std::move(hint);
        check.warning = warning;
        report.checks.push_back(std::move(check));
    };

    // config
    std::error_code ec;
    const bool configExists = !config.config_path.empty() &&
                              fs::is_regular_file(config.config_path, ec);
    add("config", configExists,
        config.config_path.empty() ? "no configuration path"
                                   : config.config_path.generic_string());

    // project root
    const bool rootOk = !config.project.root.empty() &&
                        fs::is_directory(config.project.root, ec);
    add("project.root", rootOk, config.project.root.generic_string(),
        rootOk ? "" : "Check [project].root; it must be an existing directory");

    // compilation database
    bool needsCdb = false;
    for (const auto& checker : config.checkers)
    {
        if (checker.enabled && checkerNeedsCompilationDatabase(checker.kind))
        {
            needsCdb = true;
            break;
        }
    }
    const bool cdbSet = !config.project.compilation_database.empty();
    const bool cdbOk =
        cdbSet && fs::is_regular_file(config.project.compilation_database, ec);
    if (cdbOk)
    {
        add("compilation_database", true,
            config.project.compilation_database.generic_string());
    }
    else if (needsCdb)
    {
        add("compilation_database", false,
            cdbSet ? "file not found: " +
                         config.project.compilation_database.generic_string()
                   : "not configured",
            "Configure CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and set "
            "[project].compilation_database");
    }
    else
    {
        add("compilation_database", true, "not needed by enabled checkers", "",
            true);
    }

    // report directory
    std::string writableDetail;
    const bool writable =
        !config.project.report_dir.empty() &&
        directoryWritable(config.project.report_dir, writableDetail);
    add("report_dir", writable,
        writable ? config.project.report_dir.generic_string() : writableDetail,
        writable ? "" : "Check permissions on [project].report_dir");

    // environments
    for (const auto& [name, profile] : config.environments)
    {
        if (profile.kind == EnvironmentKind::Msys2)
        {
            std::string msysError;
            const auto install = findMsys2Install(&profile, msysError);
            add("environment." + name, install.has_value(),
                install ? install->root.generic_string() : msysError,
                install ? ""
                        : "Set [environment." + name + "].root or MSYS2_ROOT");
        }
        else
        {
            add("environment." + name, true, "native process launch");
        }
    }

    // checkers
    for (const auto& checker : config.checkers)
    {
        if (!checker.enabled)
        {
            continue;
        }
        const std::string name = "checker." + checker.id;
        const auto envIt = config.environments.find(checker.environment);
        if (envIt == config.environments.end())
        {
            add(name, false,
                "unknown environment '" + checker.environment + "'");
            continue;
        }

        std::string executable = defaultExecutableForKind(checker.kind);
        if (const auto it = checker.options.find("executable");
            it != checker.options.end() && !it->second.empty())
        {
            executable = it->second;
        }

        if (!probe)
        {
            add(name, true, checker.kind + " -> " + executable);
            continue;
        }

        const std::string version =
            probe(checker.id, envIt->second, executable);
        if (version.empty())
        {
            add(name, false,
                "cannot run '" + executable + "' in environment '" +
                    checker.environment + "'",
                "Install the tool or set options.executable to its full path");
        }
        else
        {
            add(name, true, version);
        }
    }

    // baseline
    if (!config.baseline.path.empty())
    {
        if (fs::is_regular_file(config.baseline.path, ec))
        {
            BaselineData data;
            std::string baselineError;
            if (loadBaseline(config.baseline.path, data, baselineError))
            {
                add("baseline", true,
                    config.baseline.path.generic_string() + " (" +
                        std::to_string(data.findings.size()) + " findings)");
            }
            else
            {
                add("baseline", false, baselineError,
                    "Delete the file or regenerate it with --update-baseline");
            }
        }
        else
        {
            add("baseline", true,
                "not created yet: " + config.baseline.path.generic_string(),
                "Run with --update-baseline to record current findings", true);
        }
    }

    return report;
}

std::string formatDoctorText(const DoctorReport& report)
{
    std::string out;
    for (const auto& check : report.checks)
    {
        const char* status =
            !check.ok ? "FAIL" : (check.warning ? "WARN" : "ok  ");
        out += "[";
        out += status;
        out += "] ";
        out += check.name;
        if (!check.detail.empty())
        {
            out += " : ";
            out += check.detail;
        }
        out += '\n';
        if (!check.hint.empty())
        {
            out += "       hint: ";
            out += check.hint;
            out += '\n';
        }
    }
    out += '\n';
    out += report.ok() ? "result: ready to run\n" : "result: blocked\n";
    return out;
}

std::string formatDoctorJson(const DoctorReport& report)
{
    std::string out = "{\"ok\":";
    out += report.ok() ? "true" : "false";
    out += ",\"checks\":[";
    for (std::size_t i = 0; i < report.checks.size(); ++i)
    {
        const auto& check = report.checks[i];
        if (i > 0)
        {
            out.push_back(',');
        }
        out += "{\"name\":" + jsonString(check.name);
        out += ",\"ok\":";
        out += check.ok ? "true" : "false";
        out += ",\"warning\":";
        out += check.warning ? "true" : "false";
        out += ",\"detail\":" + jsonString(check.detail);
        out += ",\"hint\":" + jsonString(check.hint);
        out.push_back('}');
    }
    out += "]}";
    return out;
}

} // namespace cppsentinel
