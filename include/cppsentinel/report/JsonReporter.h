#pragma once

// Serialize the report layers to JSON documents.
//   serializeRunReportJson     -> report.raw.json (schema 2, per-checker raw diagnostics)
//   serializeUnifiedReportJson -> report.json     (schema 3, normalized findings + summary)

#include "cppsentinel/report/Findings.h"
#include "cppsentinel/report/Types.h"

#include <string>

namespace cppsentinel
{

std::string serializeRunReportJson(const RunReport& report);

std::string serializeUnifiedReportJson(const UnifiedReport& report);

} // namespace cppsentinel
