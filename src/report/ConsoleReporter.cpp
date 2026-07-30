// Console / summary.txt text derived solely from RunReport.

#include "cppsentinel/report/ConsoleReporter.h"

#include <sstream>

namespace cppsentinel
{

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

void writeConsoleSummary(const RunReport& report, std::ostream& out)
{
    out << formatSummaryText(report);
}

} // namespace cppsentinel
