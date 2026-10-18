#pragma once

// Human-readable Markdown report derived from the unified finding layer.
// This is the artifact a reviewer reads; report.json stays the machine source.

#include "cppsentinel/report/Findings.h"

#include <string>

namespace cppsentinel
{

std::string formatMarkdownReport(const UnifiedReport& report);

} // namespace cppsentinel
