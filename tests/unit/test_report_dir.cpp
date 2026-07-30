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
    auto a = createReportLayout(base, err1);
    REQUIRE(a.has_value());

    std::string err2;
    auto b = createReportLayout(base, err2);
    REQUIRE(b.has_value());

    REQUIRE(a->root != b->root);
    REQUIRE(fs::exists(a->root / "logs"));
    REQUIRE(fs::exists(b->artifacts_dir));

    // Creating the same leaf again must fail exclusivity check path internally;
    // public API always picks a fresh id.
    std::set<fs::path> roots{a->root, b->root};
    for (int i = 0; i < 5; ++i)
    {
        std::string err;
        auto extra = createReportLayout(base, err);
        REQUIRE(extra.has_value());
        REQUIRE(roots.insert(extra->root).second);
    }
}

TEST(report_writer_emits_json_and_summary)
{
    const auto base = fs::temp_directory_path() / "cppsentinel_report_tests";
    std::string err;
    auto layout = createReportLayout(base, err);
    REQUIRE(layout.has_value());

    ReportWriter writer(*layout);
    RunReport report;
    report.metadata.run_id = layout->run_id;
    report.metadata.report_dir = layout->root;
    report.exit_code = 0;

    std::string writeErr;
    REQUIRE(writer.writeReport(report, writeErr));
    REQUIRE(fs::exists(layout->root / "report.json"));
    REQUIRE(fs::exists(layout->root / "summary.txt"));
}
