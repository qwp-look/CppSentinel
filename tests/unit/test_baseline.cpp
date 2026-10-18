#include "test_main.hpp"

#include "cppsentinel/report/Baseline.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

Finding makeFinding(std::string id, std::string rule, const fs::path& file,
                    std::uint32_t line, std::string message)
{
    Finding finding;
    finding.id = std::move(id);
    finding.rule = std::move(rule);
    finding.file = file;
    finding.line = line;
    finding.severity = Severity::Warning;
    finding.category = categoryForRule(finding.rule);
    finding.message = std::move(message);
    return finding;
}

UnifiedReport makeUnified(std::vector<Finding> findings)
{
    UnifiedReport report;
    report.findings = std::move(findings);
    recomputeSummary(report);
    return report;
}

fs::path baselinePath()
{
    return fs::temp_directory_path() / "cppsentinel_baseline" / "baseline.json";
}

} // namespace

TEST(baseline_roundtrips_through_json)
{
    auto report = makeUnified({
        makeFinding("CS-AAAA", "bugprone-x", "src/a.cpp", 10, "message one"),
        makeFinding("CS-BBBB", "cert-err33-c", "src/b.cpp", 20, "message two"),
    });

    const auto data = baselineFromReport(report);
    REQUIRE(data.findings.size() == 2);

    const auto path = baselinePath();
    std::string error;
    REQUIRE(saveBaseline(path, data, error));

    BaselineData loaded;
    REQUIRE(loadBaseline(path, loaded, error));
    REQUIRE(loaded.findings.size() == 2);
    REQUIRE(loaded.findings[0].id == "CS-AAAA");
    REQUIRE(loaded.findings[0].rule == "bugprone-x");
    REQUIRE(loaded.findings[0].file == "src/a.cpp");
    REQUIRE(loaded.findings[0].line == 10);
    REQUIRE(loaded.findings[1].message == "message two");
}

TEST(baseline_marks_new_known_and_fixed)
{
    const auto previous = baselineFromReport(makeUnified({
        makeFinding("CS-AAAA", "bugprone-x", "src/a.cpp", 10, "message one"),
        makeFinding("CS-BBBB", "cert-err33-c", "src/b.cpp", 20, "message two"),
    }));

    // Current run: one unchanged, one moved line (same rule/message), one new.
    auto current = makeUnified({
        makeFinding("CS-AAAA", "bugprone-x", "src/a.cpp", 10, "message one"),
        makeFinding("CS-BBBB-MOVED", "cert-err33-c", "src/b.cpp", 25,
                    "message two"),
        makeFinding("CS-CCCC", "readability-y", "src/c.cpp", 3, "brand new"),
    });

    applyBaseline(current, previous, "baseline.json");

    REQUIRE(current.baseline.enabled);
    REQUIRE(current.baseline.known_findings == 2);
    REQUIRE(current.baseline.new_findings == 1);
    REQUIRE(current.baseline.fixed_findings == 0);

    for (const auto& finding : current.findings)
    {
        if (finding.rule == "readability-y")
        {
            REQUIRE(finding.baseline == BaselineStatus::New);
        }
        else
        {
            REQUIRE(finding.baseline == BaselineStatus::Known);
        }
    }
}

TEST(baseline_reports_fixed_findings)
{
    const auto previous = baselineFromReport(makeUnified({
        makeFinding("CS-AAAA", "bugprone-x", "src/a.cpp", 10, "message one"),
        makeFinding("CS-BBBB", "cert-err33-c", "src/b.cpp", 20, "message two"),
    }));

    auto current = makeUnified({
        makeFinding("CS-AAAA", "bugprone-x", "src/a.cpp", 10, "message one"),
    });

    applyBaseline(current, previous, "baseline.json");

    REQUIRE(current.baseline.known_findings == 1);
    REQUIRE(current.baseline.fixed_findings == 1);
    REQUIRE(current.baseline.fixed.size() == 1);
    REQUIRE(current.baseline.fixed[0].id == "CS-BBBB");
    REQUIRE(current.baseline.fixed[0].file.generic_string() == "src/b.cpp");
}

TEST(baseline_load_rejects_broken_file)
{
    const auto path = baselinePath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{ not json";
    }

    BaselineData data;
    std::string error;
    REQUIRE(!loadBaseline(path, data, error));
    REQUIRE(!error.empty());
}
