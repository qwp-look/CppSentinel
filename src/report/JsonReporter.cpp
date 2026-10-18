// Hand-written JSON serializers for the raw (v2) and unified (v3) report layers.
// No JSON dependency: output shape is small, fixed, and stable.

#include "cppsentinel/report/JsonReporter.h"

#include "cppsentinel/report/JsonUtil.h"

#include <cstdint>
#include <optional>
#include <sstream>
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

std::string quotedPath(const std::filesystem::path& path)
{
    return jsonString(path.generic_string());
}

std::string number(long long value)
{
    return std::to_string(value);
}

std::string optionalNumber(const std::optional<std::uint32_t>& value)
{
    return value ? std::to_string(static_cast<long long>(*value)) : "null";
}

std::string optionalInt(const std::optional<int>& value)
{
    return value ? std::to_string(*value) : "null";
}

/// Accumulates "key":value members with automatic comma handling.
struct ObjectBuilder
{
    std::string body;

    void add(std::string_view key, std::string rawValue)
    {
        if (!body.empty())
        {
            body.push_back(',');
        }
        body += jsonString(key);
        body.push_back(':');
        body += std::move(rawValue);
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

std::string stringArray(const std::vector<std::string>& values)
{
    std::vector<std::string> quotedValues;
    quotedValues.reserve(values.size());
    for (const auto& value : values)
    {
        quotedValues.push_back(jsonString(value));
    }
    return arrayOf(quotedValues);
}

std::string severityCountsJson(const SeverityCounts& counts)
{
    ObjectBuilder b;
    b.add("error", number(static_cast<long long>(counts.error)));
    b.add("warning", number(static_cast<long long>(counts.warning)));
    b.add("style", number(static_cast<long long>(counts.style)));
    b.add("information", number(static_cast<long long>(counts.information)));
    b.add("unknown", number(static_cast<long long>(counts.unknown)));
    b.add("total", number(static_cast<long long>(counts.total())));
    return b.build();
}

std::string metadataJson(const RunMetadata& metadata)
{
    ObjectBuilder b;
    b.add("run_id", jsonString(metadata.run_id));
    b.add("config_path", quotedPath(metadata.config_path));
    b.add("target_path", quotedPath(metadata.target_path));
    b.add("project_root", quotedPath(metadata.project_root));
    b.add("report_dir", quotedPath(metadata.report_dir));
    b.add("started_at", jsonString(metadata.started_at));
    b.add("finished_at", jsonString(metadata.finished_at));
    b.add("duration_ms", number(metadata.duration.count()));
    return b.build();
}

} // namespace

std::string serializeRunReportJson(const RunReport& report)
{
    std::ostringstream oss;
    oss << '{';
    oss << "\"schema_version\":" << report.schema_version;
    oss << ",\"exit_code\":" << report.exit_code;
    oss << ",\"metadata\":" << metadataJson(report.metadata);

    oss << ",\"checkers\":[";
    for (std::size_t i = 0; i < report.checkers.size(); ++i)
    {
        const auto& checker = report.checkers[i];
        if (i > 0)
        {
            oss << ',';
        }
        ObjectBuilder c;
        c.add("id", jsonString(checker.checker_id));
        c.add("kind", jsonString(checker.checker_kind));
        c.add("status", jsonString(toString(checker.status)));
        c.add("exit_code", optionalInt(checker.exit_code));
        c.add("duration_ms", number(checker.duration.count()));
        c.add("message", jsonString(checker.message));
        c.add("stdout_log", quotedPath(checker.stdout_log));
        c.add("stderr_log", quotedPath(checker.stderr_log));

        std::vector<std::string> artifacts;
        artifacts.reserve(checker.artifacts.size());
        for (const auto& artifact : checker.artifacts)
        {
            artifacts.push_back(quotedPath(artifact));
        }
        c.add("artifacts", arrayOf(artifacts));

        std::vector<std::string> diagnostics;
        diagnostics.reserve(checker.diagnostics.size());
        for (const auto& diag : checker.diagnostics)
        {
            ObjectBuilder d;
            d.add("file", quotedPath(diag.file));
            d.add("line", optionalNumber(diag.line));
            d.add("column", optionalNumber(diag.column));
            d.add("severity", jsonString(toString(diag.severity)));
            d.add("message", jsonString(diag.message));
            d.add("rule", jsonString(diag.rule));
            diagnostics.push_back(d.build());
        }
        c.add("diagnostics", arrayOf(diagnostics));
        oss << c.build();
    }
    oss << ']';
    oss << '}';
    return oss.str();
}

std::string serializeUnifiedReportJson(const UnifiedReport& report)
{
    std::ostringstream oss;
    oss << '{';
    oss << "\"schema_version\":" << report.schema_version;
    oss << ",\"exit_code\":" << report.exit_code;

    ObjectBuilder tool;
    tool.add("name", jsonString("CppSentinel"));
    tool.add("layer", jsonString("unified-findings"));
    oss << ",\"tool\":" << tool.build();
    oss << ",\"metadata\":" << metadataJson(report.metadata);

    // ---- summary ----
    ObjectBuilder summary;
    summary.add("total_findings",
                number(static_cast<long long>(report.summary.total_findings)));
    summary.add("raw_diagnostics",
                number(static_cast<long long>(report.summary.raw_diagnostics)));
    summary.add("deduplicated",
                number(static_cast<long long>(report.summary.deduplicated)));
    summary.add("attached_notes",
                number(static_cast<long long>(report.summary.attached_notes)));
    summary.add("tool_notices",
                number(static_cast<long long>(report.summary.tool_notices)));
    summary.add("suppressed",
                number(static_cast<long long>(report.summary.suppressed)));
    summary.add("affected_files",
                number(static_cast<long long>(report.summary.affected_files)));
    summary.add("by_severity", severityCountsJson(report.summary.severity));

    std::vector<std::string> categories;
    categories.reserve(report.summary.categories.size());
    for (const auto& category : report.summary.categories)
    {
        ObjectBuilder c;
        c.add("category", jsonString(category.category));
        c.add("count", number(static_cast<long long>(category.count)));
        categories.push_back(c.build());
    }
    summary.add("by_category", arrayOf(categories));

    std::vector<std::string> files;
    files.reserve(report.summary.files.size());
    for (const auto& file : report.summary.files)
    {
        ObjectBuilder f;
        f.add("path", quotedPath(file.file));
        f.add("counts", severityCountsJson(file.counts));
        files.push_back(f.build());
    }
    summary.add("files", arrayOf(files));

    std::vector<std::string> rules;
    rules.reserve(report.summary.rules.size());
    for (const auto& rule : report.summary.rules)
    {
        ObjectBuilder r;
        r.add("rule", jsonString(rule.rule));
        r.add("category", jsonString(rule.category));
        r.add("severity", jsonString(toString(rule.severity)));
        r.add("count", number(static_cast<long long>(rule.count)));
        rules.push_back(r.build());
    }
    summary.add("rules", arrayOf(rules));

    std::vector<std::string> checkers;
    checkers.reserve(report.checkers.size());
    for (const auto& checker : report.checkers)
    {
        ObjectBuilder c;
        c.add("id", jsonString(checker.id));
        c.add("kind", jsonString(checker.kind));
        c.add("status", jsonString(toString(checker.status)));
        c.add("exit_code", optionalInt(checker.exit_code));
        c.add("duration_ms", number(checker.duration.count()));
        c.add("raw_diagnostics",
              number(static_cast<long long>(checker.raw_diagnostics)));
        c.add("message", jsonString(checker.message));
        c.add("stdout_log", quotedPath(checker.stdout_log));
        c.add("stderr_log", quotedPath(checker.stderr_log));
        checkers.push_back(c.build());
    }
    summary.add("checkers", arrayOf(checkers));
    oss << ",\"summary\":" << summary.build();
    // ---- findings ----
    std::vector<std::string> findings;
    findings.reserve(report.findings.size());
    for (const auto& finding : report.findings)
    {
        ObjectBuilder f;
        f.add("id", jsonString(finding.id));
        f.add("file", quotedPath(finding.file));
        f.add("line", optionalNumber(finding.line));
        f.add("column", optionalNumber(finding.column));
        f.add("severity", jsonString(toString(finding.severity)));
        f.add("category", jsonString(finding.category));
        f.add("rule", jsonString(finding.rule));
        f.add("message", jsonString(finding.message));
        f.add("tools", stringArray(finding.tools));
        f.add("notes", stringArray(finding.related_notes));
        f.add("occurrences", number(static_cast<long long>(finding.occurrences)));
        if (finding.baseline != BaselineStatus::NotCompared)
        {
            f.add("baseline", jsonString(toString(finding.baseline)));
        }
        findings.push_back(f.build());
    }
    oss << ",\"findings\":" << arrayOf(findings);

    // ---- suppressed (dropped by filters, kept for transparency) ----
    std::vector<std::string> suppressed;
    suppressed.reserve(report.suppressed.size());
    for (const auto& item : report.suppressed)
    {
        ObjectBuilder s;
        s.add("id", jsonString(item.id));
        s.add("file", quotedPath(item.file));
        s.add("line", optionalNumber(item.line));
        s.add("rule", jsonString(item.rule));
        s.add("reason", jsonString(item.reason));
        suppressed.push_back(s.build());
    }
    oss << ",\"suppressed\":" << arrayOf(suppressed);

    // ---- baseline comparison ----
    if (report.baseline.enabled)
    {
        ObjectBuilder b;
        b.add("path", quotedPath(report.baseline.path));
        b.add("new", number(static_cast<long long>(report.baseline.new_findings)));
        b.add("known",
              number(static_cast<long long>(report.baseline.known_findings)));
        b.add("fixed",
              number(static_cast<long long>(report.baseline.fixed_findings)));
        std::vector<std::string> fixedEntries;
        fixedEntries.reserve(report.baseline.fixed.size());
        for (const auto& entry : report.baseline.fixed)
        {
            ObjectBuilder entryObject;
            entryObject.add("id", jsonString(entry.id));
            entryObject.add("rule", jsonString(entry.rule));
            entryObject.add("file", quotedPath(entry.file));
            entryObject.add("line", optionalNumber(entry.line));
            fixedEntries.push_back(entryObject.build());
        }
        b.add("fixed_findings", arrayOf(fixedEntries));
        oss << ",\"baseline\":" << b.build();
    }

    oss << ",\"notes\":" << stringArray(report.notes);
    oss << '}';
    return oss.str();
}

} // namespace cppsentinel
