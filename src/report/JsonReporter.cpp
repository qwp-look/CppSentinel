// Report serializers built on nlohmann/json (third_party/nlohmann).
//
//   serializeRunReportJson     -> report.raw.json  (schema 2, compact)
//   serializeUnifiedReportJson -> report.json      (schema 3, indented)
//
// ordered_json keeps the field order stable so report diffs stay readable.

#include "cppsentinel/report/JsonReporter.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppsentinel
{
namespace
{

using Json = nlohmann::ordered_json;

Json optionalNumber(const std::optional<std::uint32_t>& value)
{
    if (!value)
    {
        return nullptr;
    }
    return *value;
}

Json optionalInt(const std::optional<int>& value)
{
    if (!value)
    {
        return nullptr;
    }
    return *value;
}

Json stringArray(const std::vector<std::string>& values)
{
    Json array = Json::array();
    for (const auto& value : values)
    {
        array.push_back(value);
    }
    return array;
}

Json severityCountsJson(const SeverityCounts& counts)
{
    Json object;
    object["error"] = counts.error;
    object["warning"] = counts.warning;
    object["style"] = counts.style;
    object["information"] = counts.information;
    object["unknown"] = counts.unknown;
    object["total"] = counts.total();
    return object;
}

Json metadataJson(const RunMetadata& metadata)
{
    Json object;
    object["run_id"] = metadata.run_id;
    object["config_path"] = metadata.config_path.generic_string();
    object["target_path"] = metadata.target_path.generic_string();
    object["project_root"] = metadata.project_root.generic_string();
    object["report_dir"] = metadata.report_dir.generic_string();
    object["started_at"] = metadata.started_at;
    object["finished_at"] = metadata.finished_at;
    object["duration_ms"] = metadata.duration.count();

    Json versions = Json::array();
    for (const auto& tool : metadata.tool_versions)
    {
        Json entry;
        entry["checker_id"] = tool.checker_id;
        entry["executable"] = tool.executable;
        entry["version"] = tool.version;
        versions.push_back(std::move(entry));
    }
    object["tool_versions"] = std::move(versions);
    return object;
}

} // namespace

std::string serializeRunReportJson(const RunReport& report)
{
    Json document;
    document["schema_version"] = report.schema_version;
    document["exit_code"] = report.exit_code;
    document["metadata"] = metadataJson(report.metadata);

    Json checkers = Json::array();
    for (const auto& checker : report.checkers)
    {
        Json item;
        item["id"] = checker.checker_id;
        item["kind"] = checker.checker_kind;
        item["status"] = toString(checker.status);
        item["exit_code"] = optionalInt(checker.exit_code);
        item["duration_ms"] = checker.duration.count();
        item["message"] = checker.message;
        item["stdout_log"] = checker.stdout_log.generic_string();
        item["stderr_log"] = checker.stderr_log.generic_string();

        Json artifacts = Json::array();
        for (const auto& artifact : checker.artifacts)
        {
            artifacts.push_back(artifact.generic_string());
        }
        item["artifacts"] = std::move(artifacts);

        Json diagnostics = Json::array();
        for (const auto& diagnostic : checker.diagnostics)
        {
            Json entry;
            entry["file"] = diagnostic.file.generic_string();
            entry["line"] = optionalNumber(diagnostic.line);
            entry["column"] = optionalNumber(diagnostic.column);
            entry["severity"] = toString(diagnostic.severity);
            entry["message"] = diagnostic.message;
            entry["rule"] = diagnostic.rule;
            diagnostics.push_back(std::move(entry));
        }
        item["diagnostics"] = std::move(diagnostics);

        checkers.push_back(std::move(item));
    }
    document["checkers"] = std::move(checkers);
    return document.dump();
}

std::string serializeUnifiedReportJson(const UnifiedReport& report)
{
    Json document;
    document["schema_version"] = report.schema_version;
    document["exit_code"] = report.exit_code;

    Json tool;
    tool["name"] = "CppSentinel";
    tool["layer"] = "unified-findings";
    document["tool"] = std::move(tool);
    document["metadata"] = metadataJson(report.metadata);

    Json summary;
    summary["total_findings"] = report.summary.total_findings;
    summary["raw_diagnostics"] = report.summary.raw_diagnostics;
    summary["deduplicated"] = report.summary.deduplicated;
    summary["attached_notes"] = report.summary.attached_notes;
    summary["tool_notices"] = report.summary.tool_notices;
    summary["suppressed"] = report.summary.suppressed;
    summary["affected_files"] = report.summary.affected_files;
    summary["by_severity"] = severityCountsJson(report.summary.severity);

    Json categories = Json::array();
    for (const auto& category : report.summary.categories)
    {
        Json entry;
        entry["category"] = category.category;
        entry["count"] = category.count;
        categories.push_back(std::move(entry));
    }
    summary["by_category"] = std::move(categories);

    Json files = Json::array();
    for (const auto& file : report.summary.files)
    {
        Json entry;
        entry["path"] = file.file.generic_string();
        entry["counts"] = severityCountsJson(file.counts);
        files.push_back(std::move(entry));
    }
    summary["files"] = std::move(files);

    Json rules = Json::array();
    for (const auto& rule : report.summary.rules)
    {
        Json entry;
        entry["rule"] = rule.rule;
        entry["category"] = rule.category;
        entry["severity"] = toString(rule.severity);
        entry["count"] = rule.count;
        rules.push_back(std::move(entry));
    }
    summary["rules"] = std::move(rules);

    Json checkers = Json::array();
    for (const auto& checker : report.checkers)
    {
        Json entry;
        entry["id"] = checker.id;
        entry["kind"] = checker.kind;
        entry["status"] = toString(checker.status);
        entry["exit_code"] = optionalInt(checker.exit_code);
        entry["duration_ms"] = checker.duration.count();
        entry["raw_diagnostics"] = checker.raw_diagnostics;
        entry["message"] = checker.message;
        entry["stdout_log"] = checker.stdout_log.generic_string();
        entry["stderr_log"] = checker.stderr_log.generic_string();
        checkers.push_back(std::move(entry));
    }
    summary["checkers"] = std::move(checkers);
    document["summary"] = std::move(summary);

    Json findings = Json::array();
    for (const auto& finding : report.findings)
    {
        Json entry;
        entry["id"] = finding.id;
        entry["file"] = finding.file.generic_string();
        entry["line"] = optionalNumber(finding.line);
        entry["column"] = optionalNumber(finding.column);
        entry["severity"] = toString(finding.severity);
        entry["category"] = finding.category;
        entry["rule"] = finding.rule;
        entry["message"] = finding.message;
        if (!finding.snippet.empty())
        {
            entry["snippet"] = finding.snippet;
        }
        entry["tools"] = stringArray(finding.tools);
        entry["notes"] = stringArray(finding.related_notes);
        entry["occurrences"] = finding.occurrences;
        if (finding.baseline != BaselineStatus::NotCompared)
        {
            entry["baseline"] = toString(finding.baseline);
        }

        if (!finding.fixes.empty())
        {
            Json fixes = Json::array();
            for (const auto& fix : finding.fixes)
            {
                Json fixEntry;
                fixEntry["description"] = fix.description;
                Json replacements = Json::array();
                for (const auto& replacement : fix.replacements)
                {
                    Json replacementEntry;
                    replacementEntry["file"] =
                        replacement.file.generic_string();
                    replacementEntry["offset"] = replacement.offset;
                    replacementEntry["length"] = replacement.length;
                    replacementEntry["text"] = replacement.text;
                    replacements.push_back(std::move(replacementEntry));
                }
                fixEntry["replacements"] = std::move(replacements);
                fixes.push_back(std::move(fixEntry));
            }
            entry["fixes"] = std::move(fixes);
        }

        findings.push_back(std::move(entry));
    }
    document["findings"] = std::move(findings);

    Json suppressed = Json::array();
    for (const auto& item : report.suppressed)
    {
        Json entry;
        entry["id"] = item.id;
        entry["file"] = item.file.generic_string();
        entry["line"] = optionalNumber(item.line);
        entry["rule"] = item.rule;
        entry["reason"] = item.reason;
        suppressed.push_back(std::move(entry));
    }
    document["suppressed"] = std::move(suppressed);

    if (report.baseline.enabled)
    {
        Json baseline;
        baseline["path"] = report.baseline.path.generic_string();
        baseline["new"] = report.baseline.new_findings;
        baseline["known"] = report.baseline.known_findings;
        baseline["fixed"] = report.baseline.fixed_findings;

        Json fixedEntries = Json::array();
        for (const auto& entry : report.baseline.fixed)
        {
            Json item;
            item["id"] = entry.id;
            item["rule"] = entry.rule;
            item["file"] = entry.file.generic_string();
            item["line"] = optionalNumber(entry.line);
            fixedEntries.push_back(std::move(item));
        }
        baseline["fixed_findings"] = std::move(fixedEntries);
        document["baseline"] = std::move(baseline);
    }

    document["notes"] = stringArray(report.notes);
    return document.dump(2) + "\n";
}

} // namespace cppsentinel
