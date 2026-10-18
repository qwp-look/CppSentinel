#pragma once

// SARIF 2.1.0 serializer for the unified finding layer.
// Lets report.sarif feed GitHub code scanning, VS Code, and other SARIF viewers.

#include "cppsentinel/report/Findings.h"

#include <string>

namespace cppsentinel
{

std::string serializeSarif(const UnifiedReport& report);

} // namespace cppsentinel
