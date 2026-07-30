#pragma once

// Serialize RunReport to a compact JSON document (report.json fact source).

#include "cppsentinel/report/Types.h"

#include <string>

namespace cppsentinel
{

std::string serializeRunReportJson(const RunReport& report);

} // namespace cppsentinel
