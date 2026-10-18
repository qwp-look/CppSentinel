// Baseline file format + comparison (see Baseline.h).
// JSON is handled by nlohmann/json (third_party/nlohmann).

#include "cppsentinel/report/Baseline.h"

#include "cppsentinel/util/FileIo.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <unordered_map>

namespace cppsentinel
{
namespace
{

using Json = nlohmann::json;

/// Key that ignores the line number: a finding that moved lines is still known.
std::string contentKey(std::string_view rule, std::string_view file,
                       std::string_view message)
{
    std::string key(rule);
    key.push_back('|');
    key += file;
    key.push_back('|');
    key += message;
    return key;
}

} // namespace

BaselineData baselineFromReport(const UnifiedReport& report)
{
    BaselineData baseline;
    baseline.version = 1;
    baseline.generated_at = report.metadata.finished_at.empty()
                                ? report.metadata.started_at
                                : report.metadata.finished_at;
    baseline.findings.reserve(report.findings.size());
    for (const auto& finding : report.findings)
    {
        BaselineEntry entry;
        entry.id = finding.id;
        entry.rule = finding.rule;
        entry.file = finding.file.generic_string();
        entry.line = finding.line ? static_cast<long long>(*finding.line) : 0;
        entry.message = finding.message;
        baseline.findings.push_back(std::move(entry));
    }
    return baseline;
}

std::string serializeBaseline(const BaselineData& baseline)
{
    Json document;
    document["version"] = baseline.version;
    document["generated_at"] = baseline.generated_at;
    document["findings"] = Json::array();
    for (const auto& entry : baseline.findings)
    {
        document["findings"].push_back(Json{{"id", entry.id},
                                            {"rule", entry.rule},
                                            {"file", entry.file},
                                            {"line", entry.line},
                                            {"message", entry.message}});
    }
    // Pretty-printed: the baseline is committed and reviewed in diffs.
    return document.dump(2) + "\n";
}

bool loadBaseline(const std::filesystem::path& path, BaselineData& baseline,
                  std::string& errorMessage)
{
    std::string readError;
    auto text = readTextFile(path, readError);
    if (!text)
    {
        errorMessage =
            "cannot read baseline '" + path.string() + "': " + readError;
        return false;
    }

    const auto document = Json::parse(*text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        errorMessage = "baseline '" + path.string() + "' is not a JSON object";
        return false;
    }

    try
    {
        baseline.version = document.value("version", 1u);
        baseline.generated_at = document.value("generated_at", std::string{});
        baseline.findings.clear();

        const auto entries = document.find("findings");
        if (entries != document.end() && entries->is_array())
        {
            baseline.findings.reserve(entries->size());
            for (const auto& item : *entries)
            {
                if (!item.is_object())
                {
                    continue;
                }
                BaselineEntry entry;
                entry.id = item.value("id", std::string{});
                entry.rule = item.value("rule", std::string{});
                entry.file = item.value("file", std::string{});
                entry.line = item.value("line", 0LL);
                entry.message = item.value("message", std::string{});
                baseline.findings.push_back(std::move(entry));
            }
        }
    }
    catch (const std::exception& ex)
    {
        errorMessage = "baseline '" + path.string() +
                       "' has an invalid field: " + ex.what();
        return false;
    }

    return true;
}

bool saveBaseline(const std::filesystem::path& path,
                  const BaselineData& baseline, std::string& errorMessage)
{
    std::error_code ec;
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return writeTextFile(path, serializeBaseline(baseline), errorMessage);
}

void applyBaseline(UnifiedReport& report, const BaselineData& baseline,
                   const std::filesystem::path& baselinePath)
{
    report.baseline = BaselineInfo{};
    report.baseline.enabled = true;
    report.baseline.path = baselinePath;

    std::unordered_map<std::string, std::size_t> byId;
    std::unordered_map<std::string, std::size_t> byContent;
    byId.reserve(baseline.findings.size());
    byContent.reserve(baseline.findings.size());

    for (std::size_t i = 0; i < baseline.findings.size(); ++i)
    {
        const auto& entry = baseline.findings[i];
        if (!entry.id.empty())
        {
            byId.emplace(entry.id, i);
        }
        byContent.emplace(contentKey(entry.rule, entry.file, entry.message), i);
    }

    std::vector<bool> matched(baseline.findings.size(), false);

    for (auto& finding : report.findings)
    {
        bool known = false;

        if (const auto it = byId.find(finding.id); it != byId.end())
        {
            known = true;
            matched[it->second] = true;
        }
        else if (const auto it = byContent.find(
                     contentKey(finding.rule, finding.file.generic_string(),
                                finding.message));
                 it != byContent.end())
        {
            // Same file/rule/message on a different line: treated as known.
            known = true;
            matched[it->second] = true;
        }

        finding.baseline = known ? BaselineStatus::Known : BaselineStatus::New;
        if (known)
        {
            ++report.baseline.known_findings;
        }
        else
        {
            ++report.baseline.new_findings;
        }
    }

    for (std::size_t i = 0; i < baseline.findings.size(); ++i)
    {
        if (matched[i])
        {
            continue;
        }
        const auto& entry = baseline.findings[i];
        FixedFinding fixed;
        fixed.id = entry.id;
        fixed.rule = entry.rule;
        fixed.file = entry.file;
        if (entry.line > 0)
        {
            fixed.line = static_cast<std::uint32_t>(entry.line);
        }
        report.baseline.fixed.push_back(std::move(fixed));
    }

    report.baseline.fixed_findings = report.baseline.fixed.size();
}

} // namespace cppsentinel
