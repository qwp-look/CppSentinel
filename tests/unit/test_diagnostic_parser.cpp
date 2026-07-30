#include "test_main.hpp"

#include "cppsentinel/checker/DiagnosticParser.h"

using namespace cppsentinel;

TEST(parse_clang_style_diagnostic)
{
    const char* text =
        "main.cpp:10:5: warning: unused variable 'x' [clang-diagnostic-unused-variable]\n"
        "C:/proj/a.cpp:1:1: error: file not found\n";

    auto diags = parseClangStyleDiagnostics(text);
    REQUIRE(diags.size() == 2);
    REQUIRE(diags[0].line == 10u);
    REQUIRE(diags[0].column == 5u);
    REQUIRE(diags[0].severity == Severity::Warning);
    REQUIRE(diags[0].rule == "clang-diagnostic-unused-variable");
    REQUIRE(diags[1].severity == Severity::Error);
    REQUIRE(diags[1].file.filename() == "a.cpp");
}

TEST(parse_cppcheck_template)
{
    const char* text =
        "foo.cpp:3:1: style: The function 'f' is never used. [unusedFunction]\n";
    auto diags = parseCppcheckDiagnostics(text);
    REQUIRE(diags.size() == 1);
    REQUIRE(diags[0].severity == Severity::Style);
    REQUIRE(diags[0].rule == "unusedFunction");
}
