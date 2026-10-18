#pragma once

// Human-readable summaries for stdout and summary.txt.
//   formatSummaryText   - raw per-checker view (RunReport)
//   formatUnifiedSummary - normalized view (UnifiedReport), used by the CLI

#include "cppsentinel/report/Findings.h"
#include "cppsentinel/report/Types.h"

#include <ostream>
#include <string>

namespace cppsentinel
{

void writeConsoleSummary(const RunReport& report, std::ostream& out);
std::string formatSummaryText(const RunReport& report);

std::string formatUnifiedSummary(const UnifiedReport& report);
void writeUnifiedConsoleSummary(const UnifiedReport& report, std::ostream& out);

} // namespace cppsentinel
