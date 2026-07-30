// v2 TOML loader: schema validation, path resolution, ordered [[checker]] list.

#include "cppsentinel/config/ConfigLoader.h"

#include "cppsentinel/process/PathUtil.h"

#include "toml.hpp"

#include <set>
#include <sstream>
#include <string_view>

namespace cppsentinel
{
namespace
{

void addError(std::vector<ConfigDiagnostic>& diags, std::string path,
              std::string message, std::string hint = {},
              std::optional<std::uint32_t> line = std::nullopt,
              std::optional<std::uint32_t> column = std::nullopt)
{
    ConfigDiagnostic d;
    d.path = std::move(path);
    d.message = std::move(message);
    d.hint = std::move(hint);
    d.line = line;
    d.column = column;
    diags.push_back(std::move(d));
}

std::optional<FailOn> parseFailOn(std::string_view value)
{
    if (iequals(value, "never"))
        return FailOn::Never;
    if (iequals(value, "error"))
        return FailOn::Error;
    if (iequals(value, "warning"))
        return FailOn::Warning;
    if (iequals(value, "style"))
        return FailOn::Style;
    if (iequals(value, "information") || iequals(value, "info"))
        return FailOn::Information;
    return std::nullopt;
}

std::string normalizeMsystem(std::string_view value)
{
    if (iequals(value, "mingw64"))
        return "MINGW64";
    if (iequals(value, "clang64"))
        return "CLANG64";
    if (iequals(value, "ucrt64"))
        return "UCRT64";
    if (iequals(value, "clangarm64"))
        return "CLANGARM64";
    if (iequals(value, "msys") || iequals(value, "msys2"))
        return "MSYS";
    // Already uppercase form
    std::string out(value);
    return out;
}

bool knownMsystem(std::string_view value)
{
    return iequals(value, "MINGW64") || iequals(value, "CLANG64") ||
           iequals(value, "UCRT64") || iequals(value, "CLANGARM64") ||
           iequals(value, "MSYS") || iequals(value, "mingw64") ||
           iequals(value, "clang64") || iequals(value, "ucrt64") ||
           iequals(value, "clangarm64") || iequals(value, "msys") ||
           iequals(value, "msys2");
}

std::vector<std::string> readStringArray(const toml::array* arr)
{
    std::vector<std::string> out;
    if (!arr)
    {
        return out;
    }
    for (const auto& item : *arr)
    {
        if (auto s = item.value<std::string>())
        {
            out.push_back(*s);
        }
    }
    return out;
}

} // namespace

ConfigLoadResult loadConfig(const std::filesystem::path& configPath)
{
    ConfigLoadResult result;
    result.config.config_path = configPath;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(configPath, ec) || ec)
    {
        addError(result.diagnostics, "config",
                 "configuration file not found: " + configPath.string(),
                 "Pass an existing file to --config");
        return result;
    }

    const auto configDir = configPath.parent_path().empty()
                               ? std::filesystem::current_path()
                               : std::filesystem::absolute(configPath.parent_path());

    toml::table table;
    try
    {
        table = toml::parse_file(configPath.string());
    }
    catch (const toml::parse_error& e)
    {
        const auto& src = e.source();
        addError(result.diagnostics, "config",
                 std::string("TOML parse error: ") + e.description().data(),
                 "Fix the syntax at the reported location",
                 static_cast<std::uint32_t>(src.begin.line),
                 static_cast<std::uint32_t>(src.begin.column));
        return result;
    }
    catch (const std::exception& e)
    {
        addError(result.diagnostics, "config",
                 std::string("failed to read configuration: ") + e.what());
        return result;
    }

    // Only v2 top-level keys are accepted (rejects legacy [MethodName] tables).
    static const std::set<std::string> allowedTop = {
        "schema_version", "project", "run", "environment", "checker"};
    for (auto&& [key, value] : table)
    {
        const std::string k(key.str());
        if (!allowedTop.count(k))
        {
            addError(result.diagnostics, k,
                     "unknown top-level key '" + k + "'",
                     "v2 allows schema_version, project, run, environment, "
                     "checker ([[checker]]) only; remove legacy [Method] tables");
        }
    }

    // schema_version
    if (auto ver = table["schema_version"].value<std::int64_t>())
    {
        if (*ver != 2)
        {
            addError(result.diagnostics, "schema_version",
                     "unsupported schema_version " + std::to_string(*ver),
                     "Set schema_version = 2");
        }
        else
        {
            result.config.schema_version = 2;
        }
    }
    else
    {
        addError(result.diagnostics, "schema_version",
                 "schema_version is required and must be integer 2",
                 "Add: schema_version = 2");
    }

    // project
    if (const auto* project = table["project"].as_table())
    {
        static const std::set<std::string> allowedProject = {
            "root", "compilation_database", "report_dir"};
        for (auto&& [key, value] : *project)
        {
            if (!allowedProject.count(std::string(key.str())))
            {
                addError(result.diagnostics, "project." + std::string(key.str()),
                         "unknown field", "Remove or rename the field");
            }
        }

        if (auto root = (*project)["root"].value<std::string>())
        {
            result.config.project.root = resolveAgainst(configDir, *root);
        }
        else
        {
            result.config.project.root = configDir.parent_path().empty()
                                             ? configDir
                                             : std::filesystem::absolute(
                                                   configDir / "..");
        }

        if (auto cdb = (*project)["compilation_database"].value<std::string>())
        {
            result.config.project.compilation_database =
                resolveAgainst(configDir, *cdb);
        }

        if (auto report = (*project)["report_dir"].value<std::string>())
        {
            result.config.project.report_dir = resolveAgainst(configDir, *report);
        }
        else
        {
            result.config.project.report_dir =
                resolveAgainst(result.config.project.root, "report");
        }
    }
    else
    {
        addError(result.diagnostics, "project",
                 "[project] table is required",
                 "Add [project] with root / report_dir");
        result.config.project.root = configDir;
        result.config.project.report_dir = resolveAgainst(configDir, "../report");
    }

    // run
    if (const auto* run = table["run"].as_table())
    {
        static const std::set<std::string> allowedRun = {
            "max_parallel", "timeout_seconds", "fail_on"};
        for (auto&& [key, value] : *run)
        {
            if (!allowedRun.count(std::string(key.str())))
            {
                addError(result.diagnostics, "run." + std::string(key.str()),
                         "unknown field");
            }
        }

        if (auto mp = (*run)["max_parallel"].value<std::int64_t>())
        {
            if (*mp != 1)
            {
                addError(result.diagnostics, "run.max_parallel",
                         "only max_parallel = 1 is supported in v2",
                         "Set max_parallel = 1 (parallel execution is deferred)");
            }
            result.config.run.max_parallel = static_cast<int>(*mp);
        }
        if (auto ts = (*run)["timeout_seconds"].value<std::int64_t>())
        {
            if (*ts <= 0)
            {
                addError(result.diagnostics, "run.timeout_seconds",
                         "timeout_seconds must be positive");
            }
            else
            {
                result.config.run.timeout_seconds = std::chrono::seconds(*ts);
            }
        }
        if (auto fo = (*run)["fail_on"].value<std::string>())
        {
            if (auto parsed = parseFailOn(*fo))
            {
                result.config.run.fail_on = *parsed;
            }
            else
            {
                addError(result.diagnostics, "run.fail_on",
                         "invalid fail_on '" + *fo + "'",
                         "Use never|error|warning|style|information");
            }
        }
    }

    // Always provide a default native environment.
    EnvironmentProfile native;
    native.name = "native";
    native.kind = EnvironmentKind::Native;
    result.config.environments.emplace("native", native);

    if (const auto* envs = table["environment"].as_table())
    {
        for (auto&& [key, value] : *envs)
        {
            const std::string name(key.str());
            const auto* et = value.as_table();
            if (!et)
            {
                addError(result.diagnostics, "environment." + name,
                         "environment entry must be a table");
                continue;
            }

            static const std::set<std::string> allowedEnv = {
                "kind", "root", "msystem", "inherit_parent", "env"};
            for (auto&& [ek, ev] : *et)
            {
                if (!allowedEnv.count(std::string(ek.str())))
                {
                    addError(result.diagnostics,
                             "environment." + name + "." + std::string(ek.str()),
                             "unknown field");
                }
            }

            EnvironmentProfile profile;
            profile.name = name;
            if (auto kind = (*et)["kind"].value<std::string>())
            {
                if (iequals(*kind, "native"))
                {
                    profile.kind = EnvironmentKind::Native;
                }
                else if (iequals(*kind, "msys2"))
                {
                    profile.kind = EnvironmentKind::Msys2;
                }
                else
                {
                    addError(result.diagnostics, "environment." + name + ".kind",
                             "invalid kind '" + *kind + "'",
                             "Use kind = \"native\" or \"msys2\"");
                }
            }
            else
            {
                addError(result.diagnostics, "environment." + name + ".kind",
                         "kind is required", "Set kind = \"msys2\" or \"native\"");
            }

            if (auto root = (*et)["root"].value<std::string>())
            {
                profile.root = resolveAgainst(configDir, *root);
            }
            if (auto msystem = (*et)["msystem"].value<std::string>())
            {
                if (!knownMsystem(*msystem))
                {
                    addError(result.diagnostics,
                             "environment." + name + ".msystem",
                             "unsupported msystem '" + *msystem + "'",
                             "Use MINGW64|CLANG64|UCRT64|CLANGARM64|MSYS");
                }
                profile.msystem = normalizeMsystem(*msystem);
            }
            else if (profile.kind == EnvironmentKind::Msys2)
            {
                profile.msystem = "MINGW64";
            }

            if (auto inherit = (*et)["inherit_parent"].value<bool>())
            {
                profile.inherit_parent = *inherit;
            }

            if (const auto* envMap = (*et)["env"].as_table())
            {
                for (auto&& [ek, ev] : *envMap)
                {
                    if (auto s = ev.value<std::string>())
                    {
                        profile.env.emplace(std::string(ek.str()), *s);
                    }
                    else
                    {
                        addError(result.diagnostics,
                                 "environment." + name + ".env." +
                                     std::string(ek.str()),
                                 "environment values must be strings");
                    }
                }
            }

#ifndef _WIN32
            if (profile.kind == EnvironmentKind::Msys2)
            {
                addError(result.diagnostics, "environment." + name,
                         "msys2 environments are not supported on this platform",
                         "Use kind = \"native\" on Linux");
            }
#endif

            result.config.environments[name] = std::move(profile);
        }
    }

    // checkers: [[checker]] array preserves declaration order
    if (const auto* checkers = table["checker"].as_array())
    {
        std::size_t index = 0;
        std::set<std::string> seenIds;
        for (const auto& item : *checkers)
        {
            const std::string prefix = "checker[" + std::to_string(index) + "]";
            const auto* ct = item.as_table();
            if (!ct)
            {
                addError(result.diagnostics, prefix, "checker entry must be a table");
                ++index;
                continue;
            }

            static const std::set<std::string> allowedChecker = {
                "id",         "kind",     "enabled", "environment",
                "extra_args", "timeout_seconds", "options",
                // common optional knobs stored in options map if present as scalars
                "checks", "header_filter", "compiler", "std", "enable",
                "suppressions", "style", "executable"};
            for (auto&& [ck, cv] : *ct)
            {
                if (!allowedChecker.count(std::string(ck.str())))
                {
                    addError(result.diagnostics, prefix + "." + std::string(ck.str()),
                             "unknown field '" + std::string(ck.str()) + "'",
                             "Remove unknown fields; tool flags go in extra_args");
                }
            }

            // Reject legacy command field explicitly
            if (ct->contains("command"))
            {
                addError(result.diagnostics, prefix + ".command",
                         "command shell templates are not supported in v2",
                         "Remove command= and use kind + extra_args");
            }

            CheckerConfig checker;
            if (auto id = (*ct)["id"].value<std::string>())
            {
                checker.id = *id;
            }
            else
            {
                addError(result.diagnostics, prefix + ".id", "id is required",
                         "Set a unique id string");
            }

            if (auto kind = (*ct)["kind"].value<std::string>())
            {
                checker.kind = *kind;
            }
            else
            {
                addError(result.diagnostics, prefix + ".kind", "kind is required",
                         "Set kind to a built-in checker (clang-tidy, ...)");
            }

            if (auto enabled = (*ct)["enabled"].value<bool>())
            {
                checker.enabled = *enabled;
            }

            if (auto environment = (*ct)["environment"].value<std::string>())
            {
                checker.environment = *environment;
            }

            if (const auto* extra = (*ct)["extra_args"].as_array())
            {
                checker.extra_args = readStringArray(extra);
                // also validate element types
                for (std::size_t i = 0; i < extra->size(); ++i)
                {
                    if (!(*extra)[i].is_string())
                    {
                        addError(result.diagnostics,
                                 prefix + ".extra_args[" + std::to_string(i) + "]",
                                 "extra_args elements must be strings");
                    }
                }
            }

            if (auto ts = (*ct)["timeout_seconds"].value<std::int64_t>())
            {
                if (*ts <= 0)
                {
                    addError(result.diagnostics, prefix + ".timeout_seconds",
                             "timeout_seconds must be positive");
                }
                else
                {
                    checker.timeout = std::chrono::seconds(*ts);
                }
            }

            // Optional scalar options
            for (const char* key :
                 {"checks", "header_filter", "compiler", "std", "enable",
                  "suppressions", "style", "executable"})
            {
                if (auto s = (*ct)[key].value<std::string>())
                {
                    checker.options.emplace(key, *s);
                }
            }

            if (!checker.id.empty())
            {
                if (!seenIds.insert(checker.id).second)
                {
                    addError(result.diagnostics, prefix + ".id",
                             "duplicate checker id '" + checker.id + "'",
                             "Checker ids must be unique; they map to log/artifact names");
                }
            }

            if (!checker.environment.empty() &&
                !result.config.environments.count(checker.environment))
            {
                addError(result.diagnostics, prefix + ".environment",
                         "unknown environment '" + checker.environment + "'",
                         "Define [environment." + checker.environment +
                             "] or use \"native\"");
            }

            result.config.checkers.push_back(std::move(checker));
            ++index;
        }
    }
    else if (table.contains("checker"))
    {
        addError(result.diagnostics, "checker",
                 "checker must be an array of tables",
                 "Use repeated [[checker]] entries");
    }
    else
    {
        addError(result.diagnostics, "checker",
                 "at least one [[checker]] entry is required");
    }

    result.ok = result.diagnostics.empty();
    return result;
}

} // namespace cppsentinel
