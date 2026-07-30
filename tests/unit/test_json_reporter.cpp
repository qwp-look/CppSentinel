#include "test_main.hpp"

#include "cppsentinel/report/JsonReporter.h"

using namespace cppsentinel;

TEST(json_report_contains_core_fields)
{
    RunReport report;
    report.schema_version = 2;
    report.exit_code = 2;
    report.metadata.run_id = "run1";
    report.metadata.target_path = "main.cpp";
    report.metadata.report_dir = "report/run1";
    report.metadata.started_at = "t0";
    report.metadata.finished_at = "t1";

    CheckerResult c;
    c.checker_id = "clang-tidy";
    c.checker_kind = "clang-tidy";
    c.status = CheckerStatus::Failed;
    c.exit_code = 1;
    c.message = "findings";
    Diagnostic d;
    d.file = "main.cpp";
    d.line = 3;
    d.severity = Severity::Warning;
    d.message = "something";
    d.rule = "bugprone-foo";
    c.diagnostics.push_back(d);
    report.checkers.push_back(c);

    const auto json = serializeRunReportJson(report);
    REQUIRE(json.find("\"schema_version\":2") != std::string::npos);
    REQUIRE(json.find("\"exit_code\":2") != std::string::npos);
    REQUIRE(json.find("clang-tidy") != std::string::npos);
    REQUIRE(json.find("bugprone-foo") != std::string::npos);
    REQUIRE(json.find("something") != std::string::npos);
}
