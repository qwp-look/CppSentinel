#include "test_main.hpp"

#include "cppsentinel/report/Filter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

fs::path filterRoot()
{
    return fs::temp_directory_path() / "cppsentinel_filter";
}

Finding makeFinding(const fs::path& file, std::uint32_t line, std::string rule,
                    std::string message)
{
    Finding finding;
    finding.id = "CS-" + rule + "-" + std::to_string(line);
    finding.file = file;
    finding.line = line;
    finding.column = 1;
    finding.severity = Severity::Warning;
    finding.category = categoryForRule(rule);
    finding.rule = std::move(rule);
    finding.message = std::move(message);
    finding.tools.push_back("clang-tidy");
    return finding;
}

UnifiedReport makeUnified(std::vector<Finding> findings)
{
    UnifiedReport report;
    report.findings = std::move(findings);
    recomputeSummary(report);
    return report;
}

} // namespace

TEST(glob_matches_rules_and_paths)
{
    REQUIRE(globMatch("modernize-*", "modernize-use-auto"));
    REQUIRE(!globMatch("modernize-*", "readability-x"));
    REQUIRE(globMatch("third_party/**", "third_party/boost/x.hpp"));
    REQUIRE(globMatch("**/*.hpp", "a/b/c.hpp"));
    REQUIRE(globMatch("**/*.hpp", "c.hpp"));
    // Single '*' must not cross a path separator.
    REQUIRE(!globMatch("src/*.cpp", "src/a/b.cpp"));
    REQUIRE(globMatch("src/?.cpp", "src/a.cpp"));
}

TEST(config_filters_drop_findings_and_record_reason)
{
    FilterSettings settings;
    settings.ignore_rules = {"modernize-*"};
    settings.ignore_paths = {"third_party/**"};
    settings.ignore_messages = {"too short"};
    settings.inline_suppressions = false;

    auto report = makeUnified({
        makeFinding("src/a.cpp", 1, "modernize-use-auto", "use auto"),
        makeFinding("third_party/boost/x.hpp", 2, "bugprone-x", "third party"),
        makeFinding("src/b.cpp", 3, "readability-identifier-length",
                    "name is too short"),
        makeFinding("src/c.cpp", 4, "cert-err33-c", "ignored return"),
    });

    const auto suppressed = applyFilters(report, settings, filterRoot());

    REQUIRE(suppressed == 3);
    REQUIRE(report.findings.size() == 1);
    REQUIRE(report.findings[0].rule == "cert-err33-c");
    REQUIRE(report.summary.suppressed == 3);
    REQUIRE(report.summary.total_findings == 1);

    bool sawRuleFilter = false;
    bool sawPathFilter = false;
    bool sawMessageFilter = false;
    for (const auto& item : report.suppressed)
    {
        if (item.reason == "config:ignore_rules")
        {
            sawRuleFilter = true;
        }
        if (item.reason == "config:ignore_paths")
        {
            sawPathFilter = true;
        }
        if (item.reason == "config:ignore_messages")
        {
            sawMessageFilter = true;
        }
    }
    REQUIRE(sawRuleFilter);
    REQUIRE(sawPathFilter);
    REQUIRE(sawMessageFilter);
}

TEST(min_severity_filter_keeps_only_severe_findings)
{
    FilterSettings settings;
    settings.has_min_severity = true;
    settings.min_severity = Severity::Warning;
    settings.inline_suppressions = false;

    auto info = makeFinding("src/a.cpp", 1, "portability-x", "info");
    info.severity = Severity::Information;
    auto warn = makeFinding("src/a.cpp", 2, "bugprone-x", "warn");
    auto error = makeFinding("src/a.cpp", 3, "cert-err33-c", "error");
    error.severity = Severity::Error;

    auto report = makeUnified({info, warn, error});
    applyFilters(report, settings, filterRoot());

    REQUIRE(report.findings.size() == 2);
    REQUIRE(report.summary.severity.error == 1);
    REQUIRE(report.summary.severity.warning == 1);
    REQUIRE(report.summary.severity.information == 0);
}

TEST(inline_suppression_comments_are_honoured)
{
    const auto root = filterRoot();
    std::error_code ec;
    fs::create_directories(root / "src", ec);
    {
        std::ofstream out(root / "src" / "a.cpp", std::ios::trunc);
        out << "// cppsentinel-disable-next-line readability-x\n";
        out << "int first;\n";
        out << "int second;\n";
        out << "// cppsentinel-disable bugprone-y\n";
        out << "int third;\n";
    }

    FilterSettings settings; // inline only
    auto report = makeUnified({
        makeFinding("src/a.cpp", 2, "readability-x", "next line suppressed"),
        makeFinding("src/a.cpp", 3, "readability-x", "not suppressed"),
        makeFinding("src/a.cpp", 5, "bugprone-y", "rest of file suppressed"),
        makeFinding("src/a.cpp", 5, "readability-x", "other rule kept"),
    });

    applyFilters(report, settings, root);

    REQUIRE(report.summary.suppressed == 2);
    REQUIRE(report.findings.size() == 2);
    for (const auto& finding : report.findings)
    {
        REQUIRE(finding.line.value_or(0) != 2);
        REQUIRE(finding.rule != "bugprone-y");
    }
    for (const auto& item : report.suppressed)
    {
        REQUIRE(item.reason == "inline:disable");
    }
}

TEST(inline_suppression_can_be_disabled)
{
    const auto root = filterRoot();
    std::error_code ec;
    fs::create_directories(root / "src", ec);
    {
        std::ofstream out(root / "src" / "b.cpp", std::ios::trunc);
        out << "// cppsentinel-disable-next-line readability-x\n";
        out << "int first;\n";
    }

    FilterSettings settings;
    settings.inline_suppressions = false;
    auto report = makeUnified({
        makeFinding("src/b.cpp", 2, "readability-x", "kept"),
    });

    applyFilters(report, settings, root);
    REQUIRE(report.summary.suppressed == 0);
    REQUIRE(report.findings.size() == 1);
}

TEST(outside_project_findings_can_be_dropped)
{
    FilterSettings settings;
    settings.ignore_outside_project = true;
    settings.inline_suppressions = false;

    auto report = makeUnified({
        makeFinding("C:/toolchain/include/c++/vector", 5, "clang-diagnostic-error",
                    "system header issue"),
        makeFinding("src/a.cpp", 1, "bugprone-x", "own code"),
    });
    REQUIRE(report.findings[0].file.is_absolute());

    applyFilters(report, settings, filterRoot());

    REQUIRE(report.summary.suppressed == 1);
    REQUIRE(report.findings.size() == 1);
    REQUIRE(report.findings[0].file.generic_string() == "src/a.cpp");
    REQUIRE(report.suppressed[0].reason == "config:outside_project");
}
