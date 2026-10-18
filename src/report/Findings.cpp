// Build the unified finding layer from raw per-checker diagnostics.
//
// Pipeline: relativize path -> unify rule/category -> collapse duplicates ->
// sort deterministically -> assign stable ids -> roll up summary.

#include "cppsentinel/report/Findings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace cppsentinel
{
namespace
{

char lowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowerAsciiCopy(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        out.push_back(lowerAscii(c));
    }
    return out;
}

std::string normalizeSeparators(std::string_view s)
{
    std::string out(s);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

/// Comparison key for paths: '/'-separated and ASCII-lowercased, so the same
/// file reported by different tools with different casing/separators matches.
std::string pathKey(std::string_view genericPath)
{
    return lowerAsciiCopy(normalizeSeparators(genericPath));
}

/// Trim and collapse internal whitespace runs to a single space.
std::string collapseWhitespace(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (char c : s)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace)
        {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(c);
    }
    return out;
}

std::uint64_t fnv1a64(std::string_view s) noexcept
{
    std::uint64_t hash = 1469598103934665603ull;
    for (char raw : s)
    {
        const auto byte = static_cast<unsigned char>(raw);
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hexId(std::uint64_t value)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string("CS-") + buf;
}

std::string dedupKey(const Finding& finding)
{
    std::string key = pathKey(finding.file.generic_string());
    key.push_back('|');
    key += finding.line ? std::to_string(*finding.line) : std::string("-");
    key.push_back(':');
    key += finding.column ? std::to_string(*finding.column) : std::string("-");
    key.push_back('|');
    key += lowerAsciiCopy(finding.rule);
    key.push_back('|');
    key += finding.message; // already whitespace-collapsed, case preserved
    return key;
}

bool isHardFailure(CheckerStatus status) noexcept
{
    switch (status)
    {
    case CheckerStatus::TimedOut:
    case CheckerStatus::StartError:
    case CheckerStatus::ToolError:
    case CheckerStatus::ConfigError:
        return true;
    case CheckerStatus::Succeeded:
    case CheckerStatus::Failed:
    case CheckerStatus::Skipped:
        return false;
    }
    return false;
}

bool startsWith(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

void countSeverity(SeverityCounts& counts, Severity severity) noexcept
{
    switch (severity)
    {
    case Severity::Error:
        ++counts.error;
        break;
    case Severity::Warning:
        ++counts.warning;
        break;
    case Severity::Style:
        ++counts.style;
        break;
    case Severity::Information:
        ++counts.information;
        break;
    case Severity::Unknown:
        ++counts.unknown;
        break;
    }
}

} // namespace

int severityRank(Severity severity) noexcept
{
    switch (severity)
    {
    case Severity::Error:
        return 0;
    case Severity::Warning:
        return 1;
    case Severity::Style:
        return 2;
    case Severity::Information:
        return 3;
    case Severity::Unknown:
        return 4;
    }
    return 4;
}

std::string categoryForRule(std::string_view rule)
{
    const std::string key = lowerAsciiCopy(rule);

    if (key == "compiler" || key == "gcc" || key == "clang" ||
        startsWith(key, "clang-diagnostic-") || startsWith(key, "gcc-diagnostic-"))
    {
        return "build";
    }
    if (key == "clang-format" || startsWith(key, "whitespace") ||
        startsWith(key, "format"))
    {
        return "style";
    }
    if (startsWith(key, "cert") || startsWith(key, "cppcoreguidelines-"))
    {
        return "security";
    }
    if (startsWith(key, "bugprone") || startsWith(key, "clang-analyzer") ||
        startsWith(key, "hicpp-"))
    {
        return "defect";
    }
    if (startsWith(key, "performance") || startsWith(key, "perf"))
    {
        return "performance";
    }
    if (startsWith(key, "readability") || startsWith(key, "modernize") ||
        startsWith(key, "portability") || startsWith(key, "misc") ||
        startsWith(key, "style"))
    {
        return "maintainability";
    }
    // cppcheck ids are free-form camelCase; map the high-signal ones.
    if (startsWith(key, "buffer") || startsWith(key, "memleak") ||
        startsWith(key, "memory") || startsWith(key, "nullpointer") ||
        startsWith(key, "uninit") || startsWith(key, "outofbounds") ||
        startsWith(key, "useafterfree") || startsWith(key, "dangling") ||
        startsWith(key, "invalidpointer") || startsWith(key, "doublefree") ||
        startsWith(key, "resourceleak") || startsWith(key, "arrayindex"))
    {
        return "security";
    }
    return "other";
}

std::filesystem::path normalizeFindingPath(const std::filesystem::path& file,
                                           const std::filesystem::path& project_root)
{
    if (file.empty())
    {
        return file;
    }

    if (file.is_relative())
    {
        return std::filesystem::path(
            normalizeSeparators(file.lexically_normal().generic_string()));
    }

    if (!project_root.empty())
    {
        const std::string fileKey = pathKey(file.generic_string());
        std::string rootKey = pathKey(project_root.generic_string());
        while (!rootKey.empty() && rootKey.back() == '/')
        {
            rootKey.pop_back();
        }
        if (!rootKey.empty() && fileKey.size() > rootKey.size() + 1 &&
            fileKey.compare(0, rootKey.size(), rootKey) == 0 &&
            fileKey[rootKey.size()] == '/')
        {
            const std::string generic = file.generic_string();
            return std::filesystem::path(normalizeSeparators(
                generic.substr(rootKey.size() + 1)));
        }
    }

    return std::filesystem::path(
        normalizeSeparators(file.lexically_normal().generic_string()));
}

UnifiedReport buildUnifiedReport(const RunReport& raw,
                                 const NormalizeOptions& options)
{
    UnifiedReport unified;
    unified.schema_version = 3;
    unified.metadata = raw.metadata;
    unified.exit_code = raw.exit_code;

    std::vector<Finding> findings;
    std::unordered_map<std::string, std::size_t> byKey;
    std::vector<std::string> outsideRoot;
    std::size_t rawDiagnostics = 0;
    std::size_t attachedNotes = 0;
    std::size_t toolNotices = 0;
    std::size_t orphanNotes = 0;

    for (const auto& checker : raw.checkers)
    {
        CheckerRunSummary run;
        run.id = checker.checker_id;
        run.kind = checker.checker_kind;
        run.status = checker.status;
        run.exit_code = checker.exit_code;
        run.duration = checker.duration;
        run.raw_diagnostics = checker.diagnostics.size();
        run.message = checker.message;
        run.stdout_log = checker.stdout_log;
        run.stderr_log = checker.stderr_log;
        unified.checkers.push_back(std::move(run));

        if (isHardFailure(checker.status))
        {
            std::string note = "checker '" + checker.checker_id + "' " +
                               toString(checker.status) + " (" +
                               checker.checker_kind + ")";
            if (!checker.message.empty())
            {
                note += ": " + checker.message;
            }
            note += " - findings from this checker may be incomplete";
            unified.notes.push_back(std::move(note));
        }

        std::optional<std::size_t> lastPrimary;
        for (const auto& diag : checker.diagnostics)
        {
            ++rawDiagnostics;

            // "note:" lines annotate the preceding diagnostic; attach instead of
            // counting them as separate findings.
            if (diag.role == DiagnosticRole::Note)
            {
                std::string note = collapseWhitespace(diag.message);
                if (note.empty())
                {
                    continue;
                }
                if (lastPrimary && *lastPrimary < findings.size())
                {
                    auto& target = findings[*lastPrimary];
                    if (std::find(target.related_notes.begin(),
                                  target.related_notes.end(),
                                  note) == target.related_notes.end())
                    {
                        target.related_notes.push_back(std::move(note));
                    }
                    ++attachedNotes;
                }
                else
                {
                    ++orphanNotes;
                    unified.notes.push_back("unattached tool note [" +
                                            checker.checker_id + "]: " + note);
                }
                continue;
            }

            // Informational messages without a real source location are tool
            // meta output (checkersReport, unmatchedSuppression, branch limits,
            // "nofile" placeholders), not code findings.
            const std::string rawFileKey = pathKey(diag.file.generic_string());
            const bool hasLocation = !rawFileKey.empty() &&
                                     rawFileKey != "nofile" && diag.line &&
                                     *diag.line > 0;
            if (!hasLocation && diag.severity == Severity::Information)
            {
                ++toolNotices;
                std::string notice = "tool notice [" + checker.checker_id + "]";
                if (!diag.rule.empty())
                {
                    notice += " " + diag.rule;
                }
                notice += ": " + collapseWhitespace(diag.message);
                unified.notes.push_back(std::move(notice));
                continue;
            }

            Finding finding;
            finding.file = normalizeFindingPath(diag.file, options.project_root);
            finding.line = (diag.line && *diag.line > 0) ? diag.line : std::nullopt;
            finding.column =
                (diag.column && *diag.column > 0) ? diag.column : std::nullopt;
            finding.severity = diag.severity;
            finding.rule = diag.rule.empty() ? checker.checker_kind : diag.rule;
            finding.category = categoryForRule(finding.rule);
            if (finding.category == "other" && finding.severity == Severity::Style)
            {
                // Tool ids we do not know, but the tool itself called it style.
                finding.category = "style";
            }
            finding.message = collapseWhitespace(diag.message);
            if (finding.message.empty())
            {
                finding.message = finding.rule.empty() ? "(no message)"
                                                       : finding.rule;
            }
            finding.tools.push_back(checker.checker_kind);

            if (!finding.file.empty() &&
                finding.file.is_absolute())
            {
                if (outsideRoot.size() < 5)
                {
                    const std::string path = finding.file.generic_string();
                    if (std::find(outsideRoot.begin(), outsideRoot.end(), path) ==
                        outsideRoot.end())
                    {
                        outsideRoot.push_back(path);
                    }
                }
            }

            if (!options.deduplicate)
            {
                lastPrimary = findings.size();
                findings.push_back(std::move(finding));
                continue;
            }

            const std::string key = dedupKey(finding);
            const auto it = byKey.find(key);
            if (it == byKey.end())
            {
                lastPrimary = findings.size();
                byKey.emplace(key, findings.size());
                findings.push_back(std::move(finding));
                continue;
            }

            lastPrimary = it->second;
            Finding& existing = findings[it->second];
            ++existing.occurrences;
            if (severityRank(finding.severity) < severityRank(existing.severity))
            {
                existing.severity = finding.severity;
            }
            for (const auto& tool : finding.tools)
            {
                if (std::find(existing.tools.begin(), existing.tools.end(), tool) ==
                    existing.tools.end())
                {
                    existing.tools.push_back(tool);
                }
            }
        }
    }

    std::sort(findings.begin(), findings.end(),
              [](const Finding& a, const Finding& b) {
                  const std::string ak = pathKey(a.file.generic_string());
                  const std::string bk = pathKey(b.file.generic_string());
                  if (ak != bk)
                  {
                      return ak < bk;
                  }
                  if (a.line != b.line)
                  {
                      return a.line.value_or(0u) < b.line.value_or(0u);
                  }
                  if (a.column != b.column)
                  {
                      return a.column.value_or(0u) < b.column.value_or(0u);
                  }
                  if (severityRank(a.severity) != severityRank(b.severity))
                  {
                      return severityRank(a.severity) < severityRank(b.severity);
                  }
                  if (a.rule != b.rule)
                  {
                      return a.rule < b.rule;
                  }
                  return a.message < b.message;
              });

    // Rollups
    std::map<std::string, std::size_t> fileIndex;
    std::map<std::string, std::size_t> ruleIndex;
    std::map<std::string, std::size_t> categoryIndex;

    std::vector<FileFindingSummary> files;
    std::vector<RuleFindingSummary> rules;
    std::vector<CategoryFindingSummary> categories;

    for (auto& finding : findings)
    {
        std::sort(finding.tools.begin(), finding.tools.end());

        finding.id = hexId(fnv1a64(dedupKey(finding)));

        countSeverity(unified.summary.severity, finding.severity);
        ++unified.summary.total_findings;

        const std::string fileKey = pathKey(finding.file.generic_string());
        auto fileIt = fileIndex.find(fileKey);
        if (fileIt == fileIndex.end())
        {
            FileFindingSummary entry;
            entry.file = finding.file;
            fileIndex.emplace(fileKey, files.size());
            files.push_back(std::move(entry));
            fileIt = fileIndex.find(fileKey);
        }
        countSeverity(files[fileIt->second].counts, finding.severity);

        const std::string ruleKey = lowerAsciiCopy(finding.rule);
        auto ruleIt = ruleIndex.find(ruleKey);
        if (ruleIt == ruleIndex.end())
        {
            RuleFindingSummary entry;
            entry.rule = finding.rule;
            entry.category = finding.category;
            entry.severity = finding.severity;
            entry.count = 1;
            ruleIndex.emplace(ruleKey, rules.size());
            rules.push_back(std::move(entry));
        }
        else
        {
            RuleFindingSummary& entry = rules[ruleIt->second];
            ++entry.count;
            if (severityRank(finding.severity) < severityRank(entry.severity))
            {
                entry.severity = finding.severity;
            }
        }

        auto catIt = categoryIndex.find(finding.category);
        if (catIt == categoryIndex.end())
        {
            categoryIndex.emplace(finding.category, categories.size());
            categories.push_back(CategoryFindingSummary{finding.category, 1});
        }
        else
        {
            ++categories[catIt->second].count;
        }
    }

    std::sort(files.begin(), files.end(),
              [](const FileFindingSummary& a, const FileFindingSummary& b) {
                  if (a.counts.total() != b.counts.total())
                  {
                      return a.counts.total() > b.counts.total();
                  }
                  if (a.counts.error != b.counts.error)
                  {
                      return a.counts.error > b.counts.error;
                  }
                  return pathKey(a.file.generic_string()) <
                         pathKey(b.file.generic_string());
              });
    std::sort(rules.begin(), rules.end(),
              [](const RuleFindingSummary& a, const RuleFindingSummary& b) {
                  if (a.count != b.count)
                  {
                      return a.count > b.count;
                  }
                  return a.rule < b.rule;
              });
    std::sort(categories.begin(), categories.end(),
              [](const CategoryFindingSummary& a, const CategoryFindingSummary& b) {
                  if (a.count != b.count)
                  {
                      return a.count > b.count;
                  }
                  return a.category < b.category;
              });

    unified.summary.raw_diagnostics = rawDiagnostics;
    unified.summary.attached_notes = attachedNotes;
    unified.summary.tool_notices = toolNotices;
    const std::size_t accounted = unified.summary.total_findings +
                                  attachedNotes + toolNotices + orphanNotes;
    unified.summary.deduplicated =
        rawDiagnostics > accounted ? rawDiagnostics - accounted : 0;
    unified.summary.affected_files = files.size();
    unified.summary.files = std::move(files);
    unified.summary.rules = std::move(rules);
    unified.summary.categories = std::move(categories);
    unified.findings = std::move(findings);

    for (const auto& path : outsideRoot)
    {
        unified.notes.push_back(
            "diagnostic path outside project root, kept absolute: " + path);
    }

    return unified;
}

} // namespace cppsentinel
