// Per-checker result cache (see ResultCache.h).

#include "cppsentinel/core/ResultCache.h"

#include "cppsentinel/report/JsonUtil.h"
#include "cppsentinel/util/FileIo.h"
#include "cppsentinel/util/Json.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;

std::uint64_t fnv1a64(std::string_view text) noexcept
{
    std::uint64_t hash = 1469598103934665603ull;
    for (char c : text)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex(std::uint64_t value)
{
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buffer);
}

std::string jsonString(std::string_view value)
{
    return '"' + json::escape(value) + '"';
}

bool skipDirectoryName(const std::string& name)
{
    return name == ".git" || name == "node_modules" || name == "report" ||
           name == "build" || name.rfind("build-", 0) == 0 ||
           (!name.empty() && name.front() == '.');
}

std::string roleName(DiagnosticRole role)
{
    return role == DiagnosticRole::Note ? "note" : "primary";
}

} // namespace

std::string hashKey(std::string_view text)
{
    return hex(fnv1a64(text));
}

std::string projectFingerprint(const fs::path& root)
{
    if (root.empty())
    {
        return {};
    }

    std::vector<std::string> entries;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        return {};
    }

    const fs::recursive_directory_iterator end;
    while (it != end)
    {
        const fs::path path = it->path();
        if (it->is_directory(ec))
        {
            if (skipDirectoryName(path.filename().string()))
            {
                it.disable_recursion_pending();
            }
            it.increment(ec);
            if (ec)
            {
                break;
            }
            continue;
        }

        if (it->is_regular_file(ec))
        {
            const std::string extension = path.extension().string();
            if (extension == ".h" || extension == ".hpp" || extension == ".hh" ||
                extension == ".hxx" || extension == ".c" || extension == ".cc" ||
                extension == ".cpp" || extension == ".cxx" || extension == ".ipp" ||
                extension == ".inl" || extension == ".tcc" || extension == ".m" ||
                extension == ".mm")
            {
                const auto size = it->file_size(ec);
                const auto writeTime = it->last_write_time(ec);
                std::string entry = path.generic_string();
                entry += '|';
                entry += std::to_string(ec ? 0 : static_cast<long long>(size));
                entry += '|';
                entry += std::to_string(
                    ec ? 0
                       : static_cast<long long>(writeTime.time_since_epoch().count()));
                entries.push_back(std::move(entry));
            }
        }

        it.increment(ec);
        if (ec)
        {
            break;
        }
    }

    if (entries.empty())
    {
        return {};
    }
    std::sort(entries.begin(), entries.end());
    std::string combined;
    for (const auto& entry : entries)
    {
        combined += entry;
        combined.push_back('\n');
    }
    return hex(fnv1a64(combined));
}

ResultCache::ResultCache(fs::path directory, bool enabled)
    : directory_(std::move(directory)), enabled_(enabled)
{
}

std::string ResultCache::keyFor(const CheckerConfig& checker,
                                const std::string& version,
                                const std::string& fingerprint) const
{
    std::string material = checker.kind;
    material.push_back('|');
    for (const auto& [key, value] : checker.options)
    {
        material += key;
        material.push_back('=');
        material += value;
        material.push_back(';');
    }
    material.push_back('|');
    for (const auto& extra : checker.extra_args)
    {
        material += extra;
        material.push_back(' ');
    }
    material.push_back('|');
    material += version;
    material.push_back('|');
    material += fingerprint;
    return hashKey(material);
}

std::optional<CheckerResult> ResultCache::load(const std::string& key) const
{
    if (!enabled_ || key.empty())
    {
        return std::nullopt;
    }

    const auto path = directory_ / (key + ".json");
    std::string readError;
    auto text = readTextFile(path, readError);
    if (!text)
    {
        return std::nullopt;
    }

    std::string parseError;
    auto root = json::parse(*text, parseError);
    if (!root)
    {
        return std::nullopt;
    }

    CheckerResult result;
    result.checker_id = root->stringOr("checker_id");
    result.checker_kind = root->stringOr("checker_kind");
    result.message = root->stringOr("message");
    result.duration = std::chrono::milliseconds(root->numberOr("duration_ms", 0));

    const long long exitCode = root->numberOr("exit_code", -1);
    if (exitCode >= 0)
    {
        result.exit_code = static_cast<int>(exitCode);
    }

    const std::string status = root->stringOr("status");
    if (status == "succeeded")
    {
        result.status = CheckerStatus::Succeeded;
    }
    else if (status == "failed")
    {
        result.status = CheckerStatus::Failed;
    }
    else if (status == "timed_out")
    {
        result.status = CheckerStatus::TimedOut;
    }
    else if (status == "start_error")
    {
        result.status = CheckerStatus::StartError;
    }
    else if (status == "tool_error")
    {
        result.status = CheckerStatus::ToolError;
    }
    else if (status == "config_error")
    {
        result.status = CheckerStatus::ConfigError;
    }
    else if (status == "skipped")
    {
        result.status = CheckerStatus::Skipped;
    }

    if (const auto* diagnostics = root->arrayOr("diagnostics"))
    {
        result.diagnostics.reserve(diagnostics->size());
        for (const auto& item : *diagnostics)
        {
            Diagnostic diagnostic;
            diagnostic.file = item.stringOr("file");
            const long long line = item.numberOr("line", 0);
            if (line > 0)
            {
                diagnostic.line = static_cast<std::uint32_t>(line);
            }
            const long long column = item.numberOr("column", 0);
            if (column > 0)
            {
                diagnostic.column = static_cast<std::uint32_t>(column);
            }
            diagnostic.message = item.stringOr("message");
            diagnostic.rule = item.stringOr("rule");
            diagnostic.role = item.stringOr("role") == "note"
                                  ? DiagnosticRole::Note
                                  : DiagnosticRole::Primary;

            const std::string severity = item.stringOr("severity");
            if (severity == "error")
            {
                diagnostic.severity = Severity::Error;
            }
            else if (severity == "warning")
            {
                diagnostic.severity = Severity::Warning;
            }
            else if (severity == "style")
            {
                diagnostic.severity = Severity::Style;
            }
            else if (severity == "information")
            {
                diagnostic.severity = Severity::Information;
            }
            else
            {
                diagnostic.severity = Severity::Unknown;
            }

            result.diagnostics.push_back(std::move(diagnostic));
        }
    }

    return result;
}

void ResultCache::store(const std::string& key, const CheckerResult& result) const
{
    if (!enabled_ || key.empty())
    {
        return;
    }

    std::error_code ec;
    fs::create_directories(directory_, ec);
    if (ec)
    {
        return;
    }

    std::string out = "{\"checker_id\":" + jsonString(result.checker_id);
    out += ",\"checker_kind\":" + jsonString(result.checker_kind);
    out += ",\"status\":" + jsonString(toString(result.status));
    out += ",\"exit_code\":";
    out += result.exit_code ? std::to_string(*result.exit_code) : "-1";
    out += ",\"duration_ms\":" + std::to_string(result.duration.count());
    out += ",\"message\":" + jsonString(result.message);
    out += ",\"diagnostics\":[";
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i)
    {
        const auto& diagnostic = result.diagnostics[i];
        if (i > 0)
        {
            out.push_back(',');
        }
        out += "{\"file\":" + jsonString(diagnostic.file.generic_string());
        out += ",\"line\":" +
               std::to_string(diagnostic.line ? *diagnostic.line : 0);
        out += ",\"column\":" +
               std::to_string(diagnostic.column ? *diagnostic.column : 0);
        out += ",\"severity\":" + jsonString(toString(diagnostic.severity));
        out += ",\"rule\":" + jsonString(diagnostic.rule);
        out += ",\"message\":" + jsonString(diagnostic.message);
        out += ",\"role\":" + jsonString(roleName(diagnostic.role));
        out += '}';
    }
    out += "]}";

    std::string writeError;
    writeTextFile(directory_ / (key + ".json"), out, writeError);
}

} // namespace cppsentinel
