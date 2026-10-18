#pragma once

// Attach the offending source line to each finding.

#include "cppsentinel/report/Findings.h"

#include <filesystem>

namespace cppsentinel
{

/// Fill Finding::snippet with the trimmed text of the finding's line.
/// Each file is read once; unreadable files leave the snippet empty.
void attachSnippets(UnifiedReport& report,
                    const std::filesystem::path& projectRoot);

} // namespace cppsentinel
