// Console / summary.txt text derived from the report layers.

#include "cppsentinel/report/ConsoleReporter.h"

#include <cstddef>
#include <sstream>

namespace cppsentinel
{
namespace
{

constexpr std::size_t kTopFiles = 5;

} // namespace

std::string formatSummaryText(const RunReport& report)
{
    std::ostringstream oss;
    oss << "CppSentinel run " << report.metadata.run_id << '\n';
    oss << "target : " << report.metadata.target_path.string() << '\n';
    oss << "report : " << report.metadata.report_dir.string() << '\n';
    oss << "exit   : " << report.exit_code << '\n';
    oss << "duration_ms : " << report.metadata.duration.count() << '\n';
    oss << '\n';

    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    std::size_t diagnostics = 0;
    for (const auto& c : report.checkers)
    {
        diagnostics += c.diagnostics.size();
        if (c.status == CheckerStatus::Succeeded)
        {
            ++succeeded;
        }
        else if (c.status == CheckerStatus::Skipped)
        {
            ++skipped;
        }
        else
        {
            ++failed;
        }

        oss << "- " << c.checker_id << " [" << c.checker_kind << "] "
            << toString(c.status);
        if (c.exit_code)
        {
            oss << " exit=" << *c.exit_code;
        }
        oss << " diags=" << c.diagnostics.size();
        oss << " " << c.duration.count() << "ms";
        if (!c.message.empty())
        {
            oss << " (" << c.message << ')';
        }
        oss << '\n';
    }

    oss << '\n';
    oss << "checkers total=" << report.checkers.size()
        << " succeeded=" << succeeded << " failed=" << failed
        << " skipped=" << skipped << " diagnostics=" << diagnostics << '\n';
    return oss.str();
}

std::string formatUnifiedSummary(const UnifiedReport& report)
{
    const auto& summary = report.summary;

    std::ostringstream oss;
    oss << "CppSentinel run " << report.metadata.run_id << '\n';
    oss << "target : " << report.metadata.target_path.generic_string() << '\n';
    oss << "report : " << report.metadata.report_dir.generic_string() << '\n';
    oss << "verdict: " << (report.exit_code == 0 ? "PASS" : "FAIL")
        << " (exit " << report.exit_code << ")\n";
    oss << "duration_ms : " << report.metadata.duration.count() << '\n';
    oss << '\n';

    oss << "findings: " << summary.total_findings << " distinct / "
        << summary.raw_diagnostics << " raw";
    if (summary.deduplicated > 0 || summary.attached_notes > 0 ||
        summary.tool_notices > 0 || summary.suppressed > 0)
    {
        oss << " (";
        bool first = true;
        auto add = [&](std::size_t count, const char* label) {
            if (count == 0)
            {
                return;
            }
            if (!first)
            {
                oss << ", ";
            }
            oss << count << ' ' << label;
            first = false;
        };
        add(summary.deduplicated, "duplicates collapsed");
        add(summary.attached_notes, "notes attached");
        add(summary.tool_notices, "tool notices");
        add(summary.suppressed, "suppressed");
        oss << ')';
    }
    oss << " across " << summary.affected_files << " files\n";

    oss << "  error=" << summary.severity.error
        << " warning=" << summary.severity.warning
        << " style=" << summary.severity.style
        << " info=" << summary.severity.information;
    if (summary.severity.unknown > 0)
    {
        oss << " unknown=" << summary.severity.unknown;
    }
    oss << '\n';

    bool firstCategory = true;
    for (const auto& category : summary.categories)
    {
        oss << (firstCategory ? "  categories: " : "             ");
        oss << category.category << '=' << category.count << ' ';
        firstCategory = false;
    }
    if (!firstCategory)
    {
        oss << '\n';
    }

    if (report.baseline.enabled)
    {
        oss << "baseline: new=" << report.baseline.new_findings
            << " known=" << report.baseline.known_findings
            << " fixed=" << report.baseline.fixed_findings << '\n';
        oss << "          " << report.baseline.path.generic_string() << '\n';
    }
    oss << '\n';

    oss << "tools:\n";
    for (const auto& checker : report.checkers)
    {
        oss << "  - " << checker.id << " [" << checker.kind << "] "
            << toString(checker.status);
        if (checker.exit_code)
        {
            oss << " exit=" << *checker.exit_code;
        }
        oss << " raw=" << checker.raw_diagnostics;
        oss << " " << checker.duration.count() << "ms";
        if (!checker.message.empty())
        {
            oss << " (" << checker.message << ')';
        }
        oss << '\n';
    }

    if (!summary.files.empty())
    {
        oss << "\ntop files:\n";
        std::size_t shown = 0;
        for (const auto& file : summary.files)
        {
            if (shown >= kTopFiles)
            {
                break;
            }
            oss << "  " << file.counts.total() << "x "
                << file.file.generic_string() << "  (error="
                << file.counts.error << " warning=" << file.counts.warning
                << " style=" << file.counts.style << ")\n";
            ++shown;
        }
        if (summary.files.size() > shown)
        {
            oss << "  ... " << (summary.files.size() - shown)
                << " more files\n";
        }
    }

    if (!report.notes.empty())
    {
        oss << "\nnotes:\n";
        for (const auto& note : report.notes)
        {
            oss << "  ! " << note << '\n';
        }
    }

    return oss.str();
}

void writeConsoleSummary(const RunReport& report, std::ostream& out)
{
    out << formatSummaryText(report);
}

void writeUnifiedConsoleSummary(const UnifiedReport& report, std::ostream& out)
{
    out << formatUnifiedSummary(report);
}

} // namespace cppsentinel
