#include "test_main.hpp"

#include "cppsentinel/report/ReportWriter.h"

#include <filesystem>
#include <set>

using namespace cppsentinel;
namespace fs = std::filesystem;

TEST(report_dir_is_unique)
{
    const auto base = fs::temp_directory_path() / "cppsentinel_report_tests";
    fs::create_directories(base);

    std::string err1;
    const auto a = REQUIRE_VALUE(createReportLayout(base, err1));

    std::string err2;
    const auto b = REQUIRE_VALUE(createReportLayout(base, err2));

    REQUIRE(a.root != b.root);
    REQUIRE(fs::exists(a.root / "logs"));
    REQUIRE(fs::exists(b.artifacts_dir));

    // Creating the same leaf again must fail exclusivity check path internally;
    // public API always picks a fresh id.
    std::set<fs::path> roots{a.root, b.root};
    for (int i = 0; i < 5; ++i)
    {
        std::string err;
        const auto extra = REQUIRE_VALUE(createReportLayout(base, err));
        REQUIRE(roots.insert(extra.root).second);
    }
}

TEST(report_writer_emits_json_and_summary)
{
    const auto base = fs::temp_directory_path() / "cppsentinel_report_tests";
    std::string err;
    const auto layout = REQUIRE_VALUE(createReportLayout(base, err));

    ReportWriter writer(layout);
    RunReport report;
    report.metadata.run_id = layout.run_id;
    report.metadata.report_dir = layout.root;
    report.exit_code = 0;

    std::string writeErr;
    REQUIRE(writer.writeReport(report, writeErr));
    REQUIRE(fs::exists(layout.root / "report.json"));
    REQUIRE(fs::exists(layout.root / "summary.txt"));
}
