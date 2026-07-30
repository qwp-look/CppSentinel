#pragma once

// Human-readable run summary for stdout and summary.txt (same text).

#include "cppsentinel/report/Types.h"

#include <ostream>
#include <string>

namespace cppsentinel
{

void writeConsoleSummary(const RunReport& report, std::ostream& out);
std::string formatSummaryText(const RunReport& report);

} // namespace cppsentinel
