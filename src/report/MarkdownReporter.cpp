// Markdown report: overview -> tool runs -> rollups -> per-file findings.

#include "cppsentinel/report/MarkdownReporter.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cppsentinel
{
namespace
{

/// Table-safe cell: escape pipes, drop line breaks, keep it on one row.
std::string cell(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        if (c == '|')
        {
            out += "\\|";
        }
        else if (c == '\n' || c == '\r')
        {
            out.push_back(' ');
        }
        else if (c == '`')
        {
            out.push_back('\'');
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

std::string code(std::string_view value)
{
    return "`" + cell(value) + "`";
}

/// Inline (non-table) text: keep punctuation, only flatten line breaks.
std::string inlineText(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        out.push_back((c == '\n' || c == '\r') ? ' ' : c);
    }
    return out;
}

const char* verdictText(const UnifiedReport& report)
{
    return report.exit_code == 0 ? "PASS" : "FAIL";
}

void writingHeader(std::ostringstream& oss, const UnifiedReport& report)
{
    oss << "# CppSentinel report\n\n";
    oss << "| | |\n|---|---|\n";
    oss << "| Verdict | **" << verdictText(report) << "** (exit "
        << report.exit_code << ") |\n";
    oss << "| Run id | " << code(report.metadata.run_id) << " |\n";
    oss << "| Target | " << code(report.metadata.target_path.generic_string())
        << " |\n";
    oss << "| Project root | "
        << code(report.metadata.project_root.generic_string()) << " |\n";
    oss << "| Config | " << code(report.metadata.config_path.generic_string())
        << " |\n";
    oss << "| Started | " << code(report.metadata.started_at) << " |\n";
    oss << "| Duration | " << report.metadata.duration.count() << " ms |\n";
    oss << '\n';
}

void writingOverview(std::ostringstream& oss, const UnifiedReport& report)
{
    const auto& summary = report.summary;
    oss << "## Overview\n\n";
    oss << summary.total_findings << " distinct findings from "
        << summary.raw_diagnostics << " raw diagnostics";
    std::vector<std::string> accounted;
    if (summary.deduplicated > 0)
    {
        accounted.push_back(std::to_string(summary.deduplicated) +
                            " duplicates collapsed");
    }
    if (summary.attached_notes > 0)
    {
        accounted.push_back(std::to_string(summary.attached_notes) +
                            " notes attached");
    }
    if (summary.tool_notices > 0)
    {
        accounted.push_back(std::to_string(summary.tool_notices) +
                            " tool notices set aside");
    }
    if (!accounted.empty())
    {
        oss << " (";
        for (std::size_t i = 0; i < accounted.size(); ++i)
        {
            if (i > 0)
            {
                oss << ", ";
            }
            oss << accounted[i];
        }
        oss << ')';
    }
    oss << " across " << summary.affected_files << " files.\n\n";

    oss << "| Severity | Count |\n|---|---:|\n";
    oss << "| error | " << summary.severity.error << " |\n";
    oss << "| warning | " << summary.severity.warning << " |\n";
    oss << "| style | " << summary.severity.style << " |\n";
    oss << "| information | " << summary.severity.information << " |\n";
    if (summary.severity.unknown > 0)
    {
        oss << "| unknown | " << summary.severity.unknown << " |\n";
    }
    oss << '\n';

    if (!summary.categories.empty())
    {
        oss << "### By category\n\n| Category | Count |\n|---|---:|\n";
        for (const auto& category : summary.categories)
        {
            oss << "| " << cell(category.category) << " | " << category.count
                << " |\n";
        }
        oss << '\n';
    }
}

void writingToolRuns(std::ostringstream& oss, const UnifiedReport& report)
{
    oss << "### Tool runs\n\n";
    oss << "| Checker | Kind | Status | Exit | Duration | Raw | Note |\n";
    oss << "|---|---|---|---:|---:|---:|---|\n";
    for (const auto& checker : report.checkers)
    {
        oss << "| " << code(checker.id) << " | " << cell(checker.kind) << " | "
            << toString(checker.status) << " | "
            << (checker.exit_code ? std::to_string(*checker.exit_code) : "-")
            << " | " << checker.duration.count() << " ms | "
            << checker.raw_diagnostics << " | " << cell(checker.message)
            << " |\n";
    }
    oss << '\n';
}

void writingTopRules(std::ostringstream& oss, const UnifiedReport& report)
{
    if (report.summary.rules.empty())
    {
        return;
    }
    oss << "### Top rules\n\n| Rule | Category | Severity | Count |\n";
    oss << "|---|---|---|---:|\n";
    std::size_t shown = 0;
    for (const auto& rule : report.summary.rules)
    {
        if (shown >= 15)
        {
            break;
        }
        oss << "| " << code(rule.rule) << " | " << cell(rule.category) << " | "
            << toString(rule.severity) << " | " << rule.count << " |\n";
        ++shown;
    }
    if (report.summary.rules.size() > shown)
    {
        oss << "| _" << (report.summary.rules.size() - shown)
            << " more rules in report.json_ | | | |\n";
    }
    oss << '\n';
}

void writingFiles(std::ostringstream& oss, const UnifiedReport& report)
{
    if (report.summary.files.empty())
    {
        return;
    }
    oss << "### Files with most findings\n\n";
    oss << "| File | error | warning | style | info | total |\n";
    oss << "|---|---:|---:|---:|---:|---:|\n";
    std::size_t shown = 0;
    for (const auto& file : report.summary.files)
    {
        if (shown >= 20)
        {
            break;
        }
        oss << "| " << code(file.file.generic_string()) << " | "
            << file.counts.error << " | " << file.counts.warning << " | "
            << file.counts.style << " | " << file.counts.information << " | "
            << file.counts.total() << " |\n";
        ++shown;
    }
    if (report.summary.files.size() > shown)
    {
        oss << "| _" << (report.summary.files.size() - shown)
            << " more files in report.json_ | | | | | |\n";
    }
    oss << '\n';
}

void writingFindings(std::ostringstream& oss, const UnifiedReport& report)
{
    oss << "## Findings\n\n";
    if (report.findings.empty())
    {
        oss << "No findings.\n\n";
        return;
    }

    std::string currentFile;
    for (const auto& finding : report.findings)
    {
        const std::string file = finding.file.generic_string();
        if (file != currentFile)
        {
            if (!currentFile.empty())
            {
                oss << '\n';
            }
            currentFile = file;
            oss << "### " << code(file.empty() ? "(no file)" : file) << "\n\n";
        }

        std::string location = "no location";
        if (finding.line)
        {
            location = "L" + std::to_string(*finding.line);
            if (finding.column)
            {
                location += ":" + std::to_string(*finding.column);
            }
        }

        std::string tools;
        for (std::size_t i = 0; i < finding.tools.size(); ++i)
        {
            if (i > 0)
            {
                tools += ", ";
            }
            tools += finding.tools[i];
        }

        oss << "- **" << toString(finding.severity) << "** "
            << code(finding.rule) << " " << code(location) << " - "
            << inlineText(finding.message);
        if (finding.occurrences > 1)
        {
            oss << " (" << finding.occurrences << " occurrences)";
        }
        if (!tools.empty())
        {
            oss << " _[" << inlineText(tools) << "]_";
        }
        oss << '\n';

        for (const auto& note : finding.related_notes)
        {
            oss << "  - note: " << inlineText(note) << '\n';
        }
    }
    oss << '\n';
}

} // namespace

std::string formatMarkdownReport(const UnifiedReport& report)
{
    std::ostringstream oss;
    writingHeader(oss, report);
    writingOverview(oss, report);
    writingToolRuns(oss, report);
    writingTopRules(oss, report);
    writingFiles(oss, report);
    writingFindings(oss, report);

    if (!report.notes.empty())
    {
        oss << "## Notes\n\n";
        for (const auto& note : report.notes)
        {
            oss << "- " << note << '\n';
        }
        oss << '\n';
    }

    oss << "---\n\nGenerated by CppSentinel. Machine-readable copies: `report.json` "
           "(unified), `report.raw.json` (raw tool output), `report.sarif` "
           "(SARIF 2.1.0).\n";
    return oss.str();
}

} // namespace cppsentinel
