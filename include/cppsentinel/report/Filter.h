#pragma once

// Finding filters: config-level ignore lists plus in-source suppression comments.
//
// Supported comment directives (rule list optional, comma/space separated):
//   // cppsentinel-disable-next-line <rule>[,<rule>]   suppress the next line
//   // cppsentinel-disable <rule>[,<rule>]             suppress from here on
// A missing rule list suppresses every rule for that scope.

#include "cppsentinel/config/AppConfig.h"
#include "cppsentinel/report/Findings.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace cppsentinel
{

/// Glob match used by ignore_rules / ignore_paths.
/// `*` matches within one path segment, `**` crosses separators, `?` one char.
/// Matching is ASCII case-insensitive so Windows paths behave predictably.
bool globMatch(std::string_view pattern, std::string_view text);

/// Drop findings matching config filters or inline suppression comments.
/// Dropped findings are recorded in report.suppressed with a reason and the
/// summary is recomputed. `projectRoot` resolves relative finding paths when
/// reading sources for inline directives.
/// Returns the number of suppressed findings.
std::size_t applyFilters(UnifiedReport& report, const FilterSettings& settings,
                         const std::filesystem::path& projectRoot);

} // namespace cppsentinel
