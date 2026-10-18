// Per-checker result cache (see ResultCache.h).
// JSON is handled by nlohmann/json (third_party/nlohmann).

#include "cppsentinel/core/ResultCache.h"

#include "cppsentinel/util/FileIo.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <system_error>
#include <vector>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;
using Json = nlohmann::json;

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

bool skipDirectoryName(const std::string& name)
{
    return name == ".git" || name == "node_modules" || name == "report" ||
           name == "build" || name.rfind("build-", 0) == 0 ||
           (!name.empty() && name.front() == '.');
}

bool isSourceExtension(const std::string& extension)
{
    static const char* kExtensions[] = {".h",   ".hpp", ".hh",  ".hxx", ".c",
                                        ".cc",  ".cpp", ".cxx", ".ipp", ".inl",
                                        ".tcc", ".m",   ".mm"};
    for (const char* candidate : kExtensions)
    {
        if (extension == candidate)
        {
            return true;
        }
    }
    return false;
}

Severity severityFromName(const std::string& name)
{
    if (name == "error")
    {
        return Severity::Error;
    }
    if (name == "warning")
    {
        return Severity::Warning;
    }
    if (name == "style")
    {
        return Severity::Style;
    }
    if (name == "information")
    {
        return Severity::Information;
    }
    return Severity::Unknown;
}

CheckerStatus statusFromName(const std::string& name)
{
    if (name == "succeeded")
    {
        return CheckerStatus::Succeeded;
    }
    if (name == "failed")
    {
        return CheckerStatus::Failed;
    }
    if (name == "timed_out")
    {
        return CheckerStatus::TimedOut;
    }
    if (name == "start_error")
    {
        return CheckerStatus::StartError;
    }
    if (name == "tool_error")
    {
        return CheckerStatus::ToolError;
    }
    if (name == "config_error")
    {
        return CheckerStatus::ConfigError;
    }
    return CheckerStatus::Skipped;
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

        if (it->is_regular_file(ec) &&
            isSourceExtension(path.extension().string()))
        {
            const auto size = it->file_size(ec);
            const long long sizeValue = ec ? 0 : static_cast<long long>(size);
            const auto writeTime = it->last_write_time(ec);
            const long long stampValue =
                ec ? 0
                   : static_cast<long long>(writeTime.time_since_epoch().count());

            std::string entry = path.generic_string();
            entry.push_back('|');
            entry += std::to_string(sizeValue);
            entry.push_back('|');
            entry += std::to_string(stampValue);
            entries.push_back(std::move(entry));
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

    const auto document = Json::parse(*text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        return std::nullopt;
    }

    CheckerResult result;
    try
    {
        result.checker_id = document.value("checker_id", std::string{});
        result.checker_kind = document.value("checker_kind", std::string{});
        result.message = document.value("message", std::string{});
        result.duration =
            std::chrono::milliseconds(document.value("duration_ms", 0LL));
        result.status = statusFromName(document.value("status", std::string{}));

        const long long exitCode = document.value("exit_code", -1LL);
        if (exitCode >= 0)
        {
            result.exit_code = static_cast<int>(exitCode);
        }

        const auto diagnostics = document.find("diagnostics");
        if (diagnostics != document.end() && diagnostics->is_array())
        {
            result.diagnostics.reserve(diagnostics->size());
            for (const auto& item : *diagnostics)
            {
                if (!item.is_object())
                {
                    continue;
                }
                Diagnostic diagnostic;
                diagnostic.file = item.value("file", std::string{});
                const long long line = item.value("line", 0LL);
                if (line > 0)
                {
                    diagnostic.line = static_cast<std::uint32_t>(line);
                }
                const long long column = item.value("column", 0LL);
                if (column > 0)
                {
                    diagnostic.column = static_cast<std::uint32_t>(column);
                }
                diagnostic.severity =
                    severityFromName(item.value("severity", std::string{}));
                diagnostic.rule = item.value("rule", std::string{});
                diagnostic.message = item.value("message", std::string{});
                diagnostic.role = item.value("role", std::string{}) == "note"
                                      ? DiagnosticRole::Note
                                      : DiagnosticRole::Primary;
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    catch (const std::exception&)
    {
        return std::nullopt;
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

    Json document;
    document["checker_id"] = result.checker_id;
    document["checker_kind"] = result.checker_kind;
    document["status"] = toString(result.status);
    document["exit_code"] = result.exit_code ? *result.exit_code : -1;
    document["duration_ms"] = result.duration.count();
    document["message"] = result.message;
    document["diagnostics"] = Json::array();
    for (const auto& diagnostic : result.diagnostics)
    {
        document["diagnostics"].push_back(
            Json{{"file", diagnostic.file.generic_string()},
                 {"line", diagnostic.line ? *diagnostic.line : 0u},
                 {"column", diagnostic.column ? *diagnostic.column : 0u},
                 {"severity", toString(diagnostic.severity)},
                 {"rule", diagnostic.rule},
                 {"message", diagnostic.message},
                 {"role", diagnostic.role == DiagnosticRole::Note ? "note"
                                                                  : "primary"}});
    }

    std::string writeError;
    writeTextFile(directory_ / (key + ".json"), document.dump(), writeError);
}

} // namespace cppsentinel
