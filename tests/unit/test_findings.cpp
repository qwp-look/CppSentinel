#include "test_main.hpp"

#include "cppsentinel/report/Findings.h"
#include "cppsentinel/report/JsonReporter.h"
#include "cppsentinel/report/MarkdownReporter.h"
#include "cppsentinel/report/SarifReporter.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

fs::path testRoot()
{
    return fs::temp_directory_path() / "cppsentinel_findings";
}

Diagnostic makeDiag(const fs::path& file, std::uint32_t line, Severity severity,
                    std::string rule, std::string message)
{
    Diagnostic d;
    d.file = file;
    d.line = line;
    d.column = 3;
    d.severity = severity;
    d.rule = std::move(rule);
    d.message = std::move(message);
    return d;
}

CheckerResult makeChecker(std::string id, CheckerStatus status)
{
    CheckerResult c;
    c.checker_id = id;
    c.checker_kind = id;
    c.status = status;
    return c;
}

RunReport makeReport(std::vector<CheckerResult> checkers)
{
    RunReport report;
    report.metadata.run_id = "run-test";
    report.metadata.project_root = testRoot();
    report.exit_code = 2;
    report.checkers = std::move(checkers);
    return report;
}

NormalizeOptions options()
{
    NormalizeOptions o;
    o.project_root = testRoot();
    return o;
}

} // namespace

TEST(finding_path_is_made_project_relative)
{
    const auto absolute = testRoot() / "src" / "a.cpp";
    const auto relative = normalizeFindingPath(absolute, testRoot());
    REQUIRE(relative.is_relative());
    REQUIRE(relative.generic_string() == "src/a.cpp");

    // A path outside the root stays absolute.
    const auto outside = fs::path(fs::temp_directory_path() / "other" / "b.hpp");
    const auto kept = normalizeFindingPath(outside, testRoot());
    REQUIRE(kept.is_absolute());
}

TEST(findings_collapse_duplicates_across_tools)
{
    const auto file = testRoot() / "src" / "a.cpp";

    auto tidy = makeChecker("tidy", CheckerStatus::Succeeded);
    tidy.diagnostics.push_back(
        makeDiag(file, 10, Severity::Warning, "bugprone-foo", "same thing"));
    tidy.diagnostics.push_back(
        makeDiag(file, 10, Severity::Warning, "bugprone-foo", "same thing"));

    auto cppcheck = makeChecker("cppcheck", CheckerStatus::Succeeded);
    cppcheck.diagnostics.push_back(
        makeDiag(file, 10, Severity::Error, "bugprone-foo", "same thing"));

    const auto unified = buildUnifiedReport(makeReport({tidy, cppcheck}), options());

    REQUIRE(unified.findings.size() == 1);
    REQUIRE(unified.findings[0].occurrences == 3);
    REQUIRE(unified.findings[0].severity == Severity::Error); // highest wins
    REQUIRE(unified.findings[0].tools.size() == 2);
    REQUIRE(unified.summary.raw_diagnostics == 3);
    REQUIRE(unified.summary.deduplicated == 2);
    REQUIRE(!unified.findings[0].id.empty());
    REQUIRE(unified.findings[0].id.rfind("CS-", 0) == 0);
}

TEST(findings_are_sorted_by_location)
{
    const auto a = testRoot() / "src" / "a.cpp";
    const auto b = testRoot() / "src" / "b.cpp";

    auto checker = makeChecker("tidy", CheckerStatus::Succeeded);
    checker.diagnostics.push_back(makeDiag(b, 2, Severity::Error, "r", "b2"));
    checker.diagnostics.push_back(makeDiag(a, 9, Severity::Warning, "r", "a9"));
    checker.diagnostics.push_back(makeDiag(b, 1, Severity::Error, "r", "b1"));

    const auto unified = buildUnifiedReport(makeReport({checker}), options());
    REQUIRE(unified.findings.size() == 3);
    REQUIRE(unified.findings[0].file.generic_string() == "src/a.cpp");
    REQUIRE(unified.findings[1].line.value_or(0) == 1);
    REQUIRE(unified.findings[2].line.value_or(0) == 2);
}

TEST(summary_rolls_up_severities_files_and_rules)
{
    const auto a = testRoot() / "src" / "a.cpp";
    const auto b = testRoot() / "include" / "b.hpp";

    auto tidy = makeChecker("tidy", CheckerStatus::Succeeded);
    tidy.diagnostics.push_back(
        makeDiag(a, 1, Severity::Error, "clang-analyzer-null", "null deref"));
    tidy.diagnostics.push_back(
        makeDiag(a, 2, Severity::Warning, "readability-x", "naming"));
    tidy.diagnostics.push_back(
        makeDiag(b, 5, Severity::Style, "clang-format", "not formatted"));

    auto cppcheck = makeChecker("cppcheck", CheckerStatus::TimedOut);
    cppcheck.message = "timeout after 5000ms";

    const auto unified = buildUnifiedReport(makeReport({tidy, cppcheck}), options());

    REQUIRE(unified.summary.severity.error == 1);
    REQUIRE(unified.summary.severity.warning == 1);
    REQUIRE(unified.summary.severity.style == 1);
    REQUIRE(unified.summary.total_findings == 3);
    REQUIRE(unified.summary.affected_files == 2);
    REQUIRE(unified.summary.categories.size() >= 3);

    // Highest-count file first.
    REQUIRE(unified.summary.files.front().file.generic_string() == "src/a.cpp");

    // A timed-out checker must be surfaced as a trust warning.
    REQUIRE(!unified.notes.empty());
    REQUIRE(unified.notes[0].find("cppcheck") != std::string::npos);
    REQUIRE(unified.notes[0].find("timed_out") != std::string::npos);
}

TEST(category_mapping_covers_tool_families)
{
    REQUIRE(categoryForRule("bugprone-use-after-move") == "defect");
    REQUIRE(categoryForRule("clang-analyzer-core.Null") == "defect");
    REQUIRE(categoryForRule("cert-err33-c") == "security");
    REQUIRE(categoryForRule("performance-for-range-copy") == "performance");
    REQUIRE(categoryForRule("readability-identifier-naming") ==
            "maintainability");
    REQUIRE(categoryForRule("clang-diagnostic-undefined-reference") == "build");
    REQUIRE(categoryForRule("clang-format") == "style");
    REQUIRE(categoryForRule("bufferAccessOutOfBounds") == "security");
}

TEST(unified_json_uses_schema_3_with_findings)
{
    const auto a = testRoot() / "src" / "a.cpp";
    auto tidy = makeChecker("tidy", CheckerStatus::Succeeded);
    tidy.diagnostics.push_back(
        makeDiag(a, 4, Severity::Warning, "bugprone-foo", "message"));

    const auto unified = buildUnifiedReport(makeReport({tidy}), options());
    const auto json = serializeUnifiedReportJson(unified);

    REQUIRE(json.find("\"schema_version\": 3") != std::string::npos);
    REQUIRE(json.find("\"findings\": [") != std::string::npos);
    REQUIRE(json.find("\"summary\": {") != std::string::npos);
    REQUIRE(json.find("\"by_severity\"") != std::string::npos);
    REQUIRE(json.find("src/a.cpp") != std::string::npos);
    REQUIRE(json.find("bugprone-foo") != std::string::npos);
    REQUIRE(json.find("\"occurrences\": 1") != std::string::npos);
}

TEST(sarif_reports_version_rule_and_location)
{
    const auto a = testRoot() / "src" / "a.cpp";
    auto tidy = makeChecker("tidy", CheckerStatus::Succeeded);
    tidy.diagnostics.push_back(
        makeDiag(a, 7, Severity::Error, "bugprone-foo", "bad move"));

    const auto unified = buildUnifiedReport(makeReport({tidy}), options());
    const auto sarif = serializeSarif(unified);

    REQUIRE(sarif.find("\"version\":\"2.1.0\"") != std::string::npos);
    REQUIRE(sarif.find("\"ruleId\":\"bugprone-foo\"") != std::string::npos);
    REQUIRE(sarif.find("\"level\":\"error\"") != std::string::npos);
    REQUIRE(sarif.find("\"startLine\":7") != std::string::npos);
    REQUIRE(sarif.find("\"uriBaseId\":\"%SRCROOT%\"") != std::string::npos);
    REQUIRE(sarif.find("src/a.cpp") != std::string::npos);
}

TEST(markdown_report_has_overview_and_findings)
{
    const auto a = testRoot() / "src" / "a.cpp";
    auto tidy = makeChecker("tidy", CheckerStatus::Succeeded);
    tidy.diagnostics.push_back(
        makeDiag(a, 3, Severity::Warning, "bugprone-foo", "pipe | in message"));

    const auto unified = buildUnifiedReport(makeReport({tidy}), options());
    const auto markdown = formatMarkdownReport(unified);

    REQUIRE(markdown.find("# CppSentinel report") != std::string::npos);
    REQUIRE(markdown.find("## Overview") != std::string::npos);
    REQUIRE(markdown.find("## Findings") != std::string::npos);
    REQUIRE(markdown.find("src/a.cpp") != std::string::npos);
    REQUIRE(markdown.find("pipe | in message") != std::string::npos);
}

TEST(note_lines_attach_and_tool_notices_are_set_aside)
{
    const auto a = testRoot() / "src" / "a.cpp";

    auto tidy = makeChecker("tidy", CheckerStatus::Succeeded);

    Diagnostic primary =
        makeDiag(a, 20, Severity::Warning, "readability-function-complexity",
                 "function is too complex");
    tidy.diagnostics.push_back(primary);

    Diagnostic note;
    note.file = a;
    note.line = 21;
    note.column = 5;
    note.severity = Severity::Information;
    note.message = "+2, including nesting penalty of 1";
    note.role = DiagnosticRole::Note;
    tidy.diagnostics.push_back(note);

    Diagnostic notice;
    notice.file = "nofile";
    notice.line = 0;
    notice.severity = Severity::Information;
    notice.rule = "checkersReport";
    notice.message = "Active checkers: 172/186";
    tidy.diagnostics.push_back(notice);

    const auto unified = buildUnifiedReport(makeReport({tidy}), options());

    REQUIRE(unified.findings.size() == 1);
    REQUIRE(unified.findings[0].related_notes.size() == 1);
    REQUIRE(unified.findings[0].related_notes[0].find("nesting penalty") !=
            std::string::npos);
    REQUIRE(unified.summary.attached_notes == 1);
    REQUIRE(unified.summary.tool_notices == 1);
    REQUIRE(unified.summary.deduplicated == 0);

    bool sawNotice = false;
    for (const auto& entry : unified.notes)
    {
        if (entry.find("tool notice") != std::string::npos)
        {
            sawNotice = true;
        }
    }
    REQUIRE(sawNotice);
}
