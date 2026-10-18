// Baseline file format + comparison (see Baseline.h).

#include "cppsentinel/report/Baseline.h"

#include "cppsentinel/util/FileIo.h"
#include "cppsentinel/report/JsonUtil.h"
#include "cppsentinel/util/Json.h"

#include <set>
#include <string>
#include <unordered_map>

namespace cppsentinel
{
namespace
{

std::string jsonString(std::string_view value)
{
    return '"' + json::escape(value) + '"';
}

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
    std::string out = "{\"version\":" + std::to_string(baseline.version);
    out += ",\"generated_at\":" + jsonString(baseline.generated_at);
    out += ",\"findings\":[";
    for (std::size_t i = 0; i < baseline.findings.size(); ++i)
    {
        const auto& entry = baseline.findings[i];
        if (i > 0)
        {
            out.push_back(',');
        }
        out += "{\"id\":" + jsonString(entry.id);
        out += ",\"rule\":" + jsonString(entry.rule);
        out += ",\"file\":" + jsonString(entry.file);
        out += ",\"line\":" + std::to_string(entry.line);
        out += ",\"message\":" + jsonString(entry.message);
        out.push_back('}');
    }
    out += "]}";
    return out;
}

bool loadBaseline(const std::filesystem::path& path, BaselineData& baseline,
                  std::string& errorMessage)
{
    std::string readError;
    auto text = readTextFile(path, readError);
    if (!text)
    {
        errorMessage = "cannot read baseline '" + path.string() + "': " + readError;
        return false;
    }

    std::string parseError;
    auto root = json::parse(*text, parseError);
    if (!root)
    {
        errorMessage = "baseline '" + path.string() + "' is not valid JSON: " +
                       parseError;
        return false;
    }

    baseline.version = static_cast<std::uint32_t>(root->numberOr("version", 1));
    baseline.generated_at = root->stringOr("generated_at");
    baseline.findings.clear();

    if (const auto* entries = root->arrayOr("findings"))
    {
        baseline.findings.reserve(entries->size());
        for (const auto& item : *entries)
        {
            BaselineEntry entry;
            entry.id = item.stringOr("id");
            entry.rule = item.stringOr("rule");
            entry.file = item.stringOr("file");
            entry.line = item.numberOr("line", 0);
            entry.message = item.stringOr("message");
            baseline.findings.push_back(std::move(entry));
        }
    }
    return true;
}

bool saveBaseline(const std::filesystem::path& path, const BaselineData& baseline,
                  std::string& errorMessage)
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
        else if (const auto it = byContent.find(contentKey(
                     finding.rule, finding.file.generic_string(),
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
