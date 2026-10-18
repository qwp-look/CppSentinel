#pragma once

// Parsing of clang-tidy `--export-fixes` YAML into structured replacements.
//
// Only the subset clang-tidy actually emits is understood: a list of
// diagnostics with a single-level `Replacements` list, plain or single/double
// quoted scalars. Block scalars and unknown constructs are skipped rather than
// guessed, so a partial file degrades to "no fixes" instead of bad edits.

#include "cppsentinel/report/Findings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cppsentinel
{

struct ParsedFix
{
    std::string rule;    ///< DiagnosticName
    std::string message; ///< Message (whitespace-collapsed)
    std::vector<FixReplacement> replacements;
};

std::vector<ParsedFix> parseClangTidyFixes(const std::string& yamlText);

/// Attach parsed fixes to findings, matching by source position first and by
/// rule + message when the position is inconclusive.
void attachFixes(UnifiedReport& report, const std::string& yamlText,
                 const std::filesystem::path& projectRoot);

} // namespace cppsentinel
