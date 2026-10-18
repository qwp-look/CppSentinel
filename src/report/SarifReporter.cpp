// SARIF 2.1.0 output derived from the unified findings (one run per CppSentinel
// run).

#include "cppsentinel/report/SarifReporter.h"

#include "cppsentinel/report/JsonUtil.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cppsentinel
{
namespace
{

std::string jsonString(std::string_view value)
{
    return '"' + json::escape(value) + '"';
}

std::string number(long long value)
{
    return std::to_string(value);
}

struct ObjectBuilder
{
    std::string body;

    void add(std::string_view key, std::string_view rawValue)
    {
        if (!body.empty())
        {
            body.push_back(',');
        }
        body += jsonString(key);
        body.push_back(':');
        body += rawValue;
    }

    std::string build() const
    {
        return "{" + body + "}";
    }
};

std::string arrayOf(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(',');
        }
        out += items[i];
    }
    out.push_back(']');
    return out;
}

/// SARIF level: only error/warning/note/none are allowed.
std::string sarifLevel(Severity severity)
{
    switch (severity)
    {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Style:
    case Severity::Information:
        return "note";
    case Severity::Unknown:
        return "none";
    }
    return "none";
}

/// file:// URI for an absolute path, with a trailing slash (usable as a base
/// id).
std::string directoryUri(const std::filesystem::path& directory)
{
    if (directory.empty())
    {
        return {};
    }
    std::string generic = directory.generic_string();
    std::string uri = json::uriEncodePath(generic);
    if (generic.size() >= 2 && generic[1] == ':')
    {
        uri = "file:///" + uri;
    }
    else
    {
        uri = "file://" + uri;
    }
    if (!uri.empty() && uri.back() != '/')
    {
        uri.push_back('/');
    }
    return uri;
}

bool anyHardFailure(const UnifiedReport& report)
{
    for (const auto& checker : report.checkers)
    {
        switch (checker.status)
        {
        case CheckerStatus::TimedOut:
        case CheckerStatus::StartError:
        case CheckerStatus::ToolError:
        case CheckerStatus::ConfigError:
            return true;
        case CheckerStatus::Succeeded:
        case CheckerStatus::Failed:
        case CheckerStatus::Skipped:
            break;
        }
    }
    return false;
}

} // namespace

std::string serializeSarif(const UnifiedReport& report)
{
    // ---- rules ----
    std::vector<std::string> rules;
    std::vector<std::string> ruleIds;
    rules.reserve(report.summary.rules.size());
    for (const auto& rule : report.summary.rules)
    {
        ruleIds.push_back(rule.rule);
        ObjectBuilder entry;
        entry.add("id", jsonString(rule.rule));
        entry.add("name", jsonString(rule.rule));
        ObjectBuilder defaultConfig;
        defaultConfig.add("level", jsonString(sarifLevel(rule.severity)));
        entry.add("defaultConfiguration", defaultConfig.build());
        ObjectBuilder properties;
        properties.add("category", jsonString(rule.category));
        entry.add("properties", properties.build());
        rules.push_back(entry.build());
    }

    auto ruleIndexOf = [&ruleIds](const std::string& rule) -> int
    {
        for (std::size_t i = 0; i < ruleIds.size(); ++i)
        {
            if (ruleIds[i] == rule)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    // ---- results ----
    std::vector<std::string> results;
    results.reserve(report.findings.size());
    for (const auto& finding : report.findings)
    {
        ObjectBuilder result;
        const int ruleIndex = ruleIndexOf(finding.rule);
        if (!finding.rule.empty())
        {
            result.add("ruleId", jsonString(finding.rule));
        }
        if (ruleIndex >= 0)
        {
            result.add("ruleIndex", number(ruleIndex));
        }
        result.add("level", jsonString(sarifLevel(finding.severity)));

        ObjectBuilder message;
        message.add("text", jsonString(finding.message));
        result.add("message", message.build());

        // Location
        ObjectBuilder artifactLocation;
        if (finding.file.is_relative())
        {
            artifactLocation.add("uri", jsonString(json::uriEncodePath(
                                            finding.file.generic_string())));
            artifactLocation.add("uriBaseId", jsonString("%SRCROOT%"));
        }
        else
        {
            artifactLocation.add("uri", jsonString(json::uriEncodePath(
                                            finding.file.generic_string())));
        }

        ObjectBuilder physicalLocation;
        physicalLocation.add("artifactLocation", artifactLocation.build());
        if (finding.line && *finding.line > 0)
        {
            ObjectBuilder region;
            region.add("startLine", number(*finding.line));
            if (finding.column && *finding.column > 0)
            {
                region.add("startColumn", number(*finding.column));
            }
            if (!finding.snippet.empty())
            {
                ObjectBuilder snippet;
                snippet.add("text", jsonString(finding.snippet));
                region.add("snippet", snippet.build());
            }
            physicalLocation.add("region", region.build());
        }
        ObjectBuilder location;
        location.add("physicalLocation", physicalLocation.build());
        result.add("locations", arrayOf({location.build()}));

        ObjectBuilder fingerprints;
        fingerprints.add("cppsentinelFindingId/v1", jsonString(finding.id));
        result.add("partialFingerprints", fingerprints.build());

        ObjectBuilder properties;
        properties.add("category", jsonString(finding.category));
        std::vector<std::string> tools;
        tools.reserve(finding.tools.size());
        for (const auto& tool : finding.tools)
        {
            tools.push_back(jsonString(tool));
        }
        properties.add("tools", arrayOf(tools));
        properties.add("occurrences",
                       number(static_cast<long long>(finding.occurrences)));
        result.add("properties", properties.build());

        if (!finding.fixes.empty())
        {
            std::vector<std::string> fixes;
            fixes.reserve(finding.fixes.size());
            for (const auto& fix : finding.fixes)
            {
                ObjectBuilder fixObject;
                ObjectBuilder description;
                description.add("text", jsonString(fix.description.empty()
                                                       ? "Apply suggested fix"
                                                       : fix.description));
                fixObject.add("description", description.build());

                // Group replacements by file: SARIF changes are per artifact.
                std::vector<std::string> order;
                std::map<std::string, std::vector<const FixReplacement*>>
                    byFile;
                for (const auto& replacement : fix.replacements)
                {
                    const std::string key = replacement.file.generic_string();
                    if (byFile.find(key) == byFile.end())
                    {
                        order.push_back(key);
                    }
                    byFile[key].push_back(&replacement);
                }

                std::vector<std::string> changes;
                for (const auto& key : order)
                {
                    ObjectBuilder change;
                    ObjectBuilder location;
                    location.add("uri", jsonString(json::uriEncodePath(key)));
                    if (!key.empty() && key.front() != '/')
                    {
                        location.add("uriBaseId", jsonString("%SRCROOT%"));
                    }
                    change.add("artifactLocation", location.build());

                    std::vector<std::string> replacements;
                    for (const auto* replacement : byFile[key])
                    {
                        ObjectBuilder entry;
                        ObjectBuilder deletedRegion;
                        deletedRegion.add("charOffset",
                                          number(static_cast<long long>(
                                              replacement->offset)));
                        deletedRegion.add("charLength",
                                          number(static_cast<long long>(
                                              replacement->length)));
                        entry.add("deletedRegion", deletedRegion.build());
                        ObjectBuilder inserted;
                        inserted.add("text", jsonString(replacement->text));
                        entry.add("insertedContent", inserted.build());
                        replacements.push_back(entry.build());
                    }
                    change.add("replacements", arrayOf(replacements));
                    changes.push_back(change.build());
                }
                fixObject.add("artifactChanges", arrayOf(changes));
                fixes.push_back(fixObject.build());
            }
            result.add("fixes", arrayOf(fixes));
        }

        results.push_back(result.build());
    }

    // ---- invocation ----
    std::vector<std::string> notifications;
    for (const auto& note : report.notes)
    {
        ObjectBuilder notification;
        notification.add("level", jsonString("warning"));
        ObjectBuilder message;
        message.add("text", jsonString(note));
        notification.add("message", message.build());
        notifications.push_back(notification.build());
    }

    ObjectBuilder invocation;
    invocation.add("executionSuccessful",
                   anyHardFailure(report) ? "false" : "true");
    ObjectBuilder invocationProps;
    invocationProps.add("runId", jsonString(report.metadata.run_id));
    invocationProps.add("startTimeUtc", jsonString(report.metadata.started_at));
    invocationProps.add("endTimeUtc", jsonString(report.metadata.finished_at));
    invocation.add("properties", invocationProps.build());
    if (!notifications.empty())
    {
        invocation.add("toolExecutionNotifications", arrayOf(notifications));
    }

    // ---- driver / run ----
    ObjectBuilder driver;
    driver.add("name", jsonString("CppSentinel"));
    driver.add("informationUri",
               jsonString("https://github.com/qwp-look/CppSentinel"));
    driver.add("rules", arrayOf(rules));

    ObjectBuilder tool;
    tool.add("driver", driver.build());

    ObjectBuilder run;
    run.add("tool", tool.build());
    run.add("invocations", arrayOf({invocation.build()}));
    run.add("results", arrayOf(results));
    if (!report.metadata.project_root.empty())
    {
        ObjectBuilder baseId;
        baseId.add("uri",
                   jsonString(directoryUri(report.metadata.project_root)));
        ObjectBuilder baseIds;
        baseIds.add("%SRCROOT%", baseId.build());
        run.add("originalUriBaseIds", baseIds.build());
    }

    ObjectBuilder root;
    root.add("$schema",
             jsonString("https://json.schemastore.org/sarif-2.1.0.json"));
    root.add("version", jsonString("2.1.0"));
    root.add("runs", arrayOf({run.build()}));
    return root.build();
}

} // namespace cppsentinel
