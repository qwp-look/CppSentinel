#pragma once

// Unified, tool-independent view of a run.
//
// RunReport keeps the raw per-checker diagnostics (traceability). This layer turns
// them into a single normalized finding list: paths made project-relative,
// severities and rule ids unified, duplicates collapsed, stable ids assigned and
// rollups computed. Every reporter (json/sarif/markdown/console) reads this
// instead of re-deriving from tool output.

#include "cppsentinel/report/Types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppsentinel
{

/// Severity histogram.
struct SeverityCounts
{
    std::size_t error = 0;
    std::size_t warning = 0;
    std::size_t style = 0;
    std::size_t information = 0;
    std::size_t unknown = 0;

    std::size_t total() const noexcept
    {
        return error + warning + style + information + unknown;
    }
};

/// Per-file rollup.
struct FileFindingSummary
{
    std::filesystem::path file;
    SeverityCounts counts;
};

/// Per-rule rollup.
struct RuleFindingSummary
{
    std::string rule;
    std::string category;
    Severity severity = Severity::Unknown;
    std::size_t count = 0;
};

/// Per-category rollup (build / defect / security / performance /
/// maintainability / style / other).
struct CategoryFindingSummary
{
    std::string category;
    std::size_t count = 0;
};

/// One checker invocation as recorded in the unified report.
struct CheckerRunSummary
{
    std::string id;
    std::string kind;
    CheckerStatus status = CheckerStatus::Failed;
    std::optional<int> exit_code;
    std::chrono::milliseconds duration{0};
    std::size_t raw_diagnostics = 0;
    std::string message;
    std::filesystem::path stdout_log;
    std::filesystem::path stderr_log;
};

/// Run-level rollup shared by all reporters.
struct FindingsSummary
{
    std::size_t total_findings = 0;  ///< distinct findings after dedup
    std::size_t raw_diagnostics = 0; ///< diagnostics seen before normalization
    std::size_t deduplicated = 0;    ///< raw collapsed into duplicate findings
    std::size_t attached_notes = 0;  ///< "note:" lines merged as finding context
    std::size_t tool_notices = 0;    ///< tool meta messages kept out of findings
    std::size_t suppressed = 0;      ///< findings dropped by filters
    std::size_t affected_files = 0;
    SeverityCounts severity;
    std::vector<FileFindingSummary> files; ///< sorted: total desc, then path
    std::vector<RuleFindingSummary> rules; ///< sorted: count desc, then rule
    std::vector<CategoryFindingSummary> categories; ///< sorted: count desc
};

/// Where a finding stands relative to the configured baseline.
enum class BaselineStatus : std::uint8_t
{
    NotCompared, ///< no baseline configured
    New,         ///< not present in the baseline
    Known        ///< accepted as pre-existing
};

inline const char* toString(BaselineStatus status) noexcept
{
    switch (status)
    {
    case BaselineStatus::NotCompared:
        return "";
    case BaselineStatus::New:
        return "new";
    case BaselineStatus::Known:
        return "known";
    }
    return "";
}

/// A finding that was dropped by a filter (kept for transparency, not counted).
struct SuppressedFinding
{
    std::string id;
    std::filesystem::path file;
    std::optional<std::uint32_t> line;
    std::string rule;
    std::string reason; ///< e.g. "config:ignore_rules" or "inline:disable-next-line"
};

/// A baseline entry that no longer appears in this run.
struct FixedFinding
{
    std::string id;
    std::string rule;
    std::filesystem::path file;
    std::optional<std::uint32_t> line;
};

struct BaselineInfo
{
    bool enabled = false;
    std::filesystem::path path;
    std::size_t new_findings = 0;
    std::size_t known_findings = 0;
    std::size_t fixed_findings = 0;
    std::vector<FixedFinding> fixed;
};

/// One machine-applicable text replacement (clang-tidy fix-it).
struct FixReplacement
{
    std::filesystem::path file;
    long long offset = 0; ///< byte offset as reported by the tool
    long long length = 0;
    std::string text;     ///< replacement text (may be empty = deletion)
};

struct Fix
{
    std::string description;
    std::vector<FixReplacement> replacements;
};

/// A normalized finding: one row in the report, independent of which tool found it.
struct Finding
{
    std::string id; ///< stable fingerprint, e.g. CS-1A2B3C4D5E6F
    std::filesystem::path file; ///< project-root relative when resolvable
    std::optional<std::uint32_t> line;
    std::optional<std::uint32_t> column;
    Severity severity = Severity::Unknown;
    std::string category;
    std::string rule; ///< tool rule id; falls back to checker kind
    std::string message;
    std::string snippet; ///< trimmed source line at `line`, when readable
    std::vector<std::string> tools; ///< contributing checker kinds, sorted
    std::vector<std::string> related_notes; ///< attached "note:" context lines
    std::size_t occurrences = 1;    ///< collapsed duplicates (e.g. header per TU)
    BaselineStatus baseline = BaselineStatus::NotCompared;
    std::vector<Fix> fixes; ///< machine-applicable replacements, when the tool has them
};

/// Comparable ordering for severity (lower rank = more severe).
int severityRank(Severity severity) noexcept;

/// Coarse category for a tool rule id.
std::string categoryForRule(std::string_view rule);

/// Make a diagnostic path project-relative and '/'-separated when possible.
/// Paths outside project_root and already-relative paths are kept as given
/// (only normalized separators).
std::filesystem::path normalizeFindingPath(const std::filesystem::path& file,
                                           const std::filesystem::path& project_root);

struct NormalizeOptions
{
    std::filesystem::path project_root;
    bool deduplicate = true;
};

struct UnifiedReport
{
    std::uint32_t schema_version = 3;
    RunMetadata metadata;
    int exit_code = 0;
    FindingsSummary summary;
    BaselineInfo baseline;
    std::vector<Finding> findings; ///< sorted by file, line, column, severity, rule
    std::vector<SuppressedFinding> suppressed; ///< dropped by filters
    std::vector<CheckerRunSummary> checkers;
    std::vector<std::string> notes; ///< normalization / trust warnings
};

UnifiedReport buildUnifiedReport(const RunReport& raw,
                                 const NormalizeOptions& options);

/// Recompute the finding-derived rollups (severity, files, rules, categories).
/// Call after mutating `findings` (filtering, suppression, baseline matching).
/// Raw counters captured at build time (raw_diagnostics, attached_notes,
/// tool_notices) are preserved.
void recomputeSummary(UnifiedReport& report);

} // namespace cppsentinel
