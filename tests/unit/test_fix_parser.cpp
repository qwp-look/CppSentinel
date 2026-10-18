#include "test_main.hpp"

#include "cppsentinel/report/FixParser.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

fs::path fixRoot()
{
    return fs::temp_directory_path() / "cppsentinel_fixes";
}

} // namespace

TEST(fix_parser_reads_diagnostics_and_replacements)
{
    const std::string yaml = R"(---
MainSourceFile:  /proj/src/a.cpp
Diagnostics:
  - DiagnosticName:  modernize-use-nodiscard
    Message:         function 'total' should be marked [[nodiscard]]
    FilePath:        /proj/src/a.cpp
    FileOffset:      42
    Replacements:
      - FilePath:        /proj/src/a.cpp
        Offset:          42
        Length:          0
        ReplacementText: '[[nodiscard]] '
  - DiagnosticName:  modernize-use-auto
    Message:         use auto when declaring iterators
    FilePath:        /proj/src/b.cpp
    FileOffset:      7
    Replacements:
      - FilePath:        /proj/src/b.cpp
        Offset:          7
        Length:          4
        ReplacementText: 'auto'
...
)";

    const auto parsed = parseClangTidyFixes(yaml);
    REQUIRE(parsed.size() == 2);
    REQUIRE(parsed[0].rule == "modernize-use-nodiscard");
    REQUIRE(parsed[0].message == "function 'total' should be marked [[nodiscard]]");
    REQUIRE(parsed[0].replacements.size() == 1);
    REQUIRE(parsed[0].replacements[0].offset == 42);
    REQUIRE(parsed[0].replacements[0].length == 0);
    REQUIRE(parsed[0].replacements[0].text == "[[nodiscard]] ");
    REQUIRE(parsed[0].replacements[0].file.generic_string() == "/proj/src/a.cpp");
    REQUIRE(parsed[1].replacements[0].text == "auto");
}

TEST(fix_parser_handles_quoted_and_escaped_text)
{
    const std::string yaml = R"(Diagnostics:
  - DiagnosticName:  readability-x
    Message:         "he said \"hi\""
    Replacements:
      - FilePath:        /p/a.cpp
        Offset:          1
        Length:          1
        ReplacementText: 'it''s'
)";

    const auto parsed = parseClangTidyFixes(yaml);
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].message == "he said \"hi\"");
    REQUIRE(parsed[0].replacements.size() == 1);
    REQUIRE(parsed[0].replacements[0].text == "it's");
}

TEST(fix_parser_ignores_malformed_input)
{
    REQUIRE(parseClangTidyFixes("").empty());
    REQUIRE(parseClangTidyFixes("not: yaml: at: all").empty());
    REQUIRE(parseClangTidyFixes("Diagnostics:\n  - Message: orphan\n").empty());
}

TEST(fixes_attach_to_findings_by_position)
{
    const auto root = fixRoot();
    std::error_code ec;
    fs::create_directories(root / "src", ec);
    const auto sourcePath = root / "src" / "a.cpp";
    {
        std::ofstream out(sourcePath, std::ios::trunc);
        out << "int one;\n";
        out << "int total() { return 0; }\n";
        out << "int three;\n";
    }

    // Offset of the second line's first character.
    const long long secondLineOffset = 9; // "int one;\n"

    const std::string yaml = "Diagnostics:\n"
                             "  - DiagnosticName:  modernize-use-nodiscard\n"
                             "    Message:         function 'total' should be marked\n"
                             "    FilePath:        " +
                             sourcePath.generic_string() +
                             "\n    FileOffset:      9\n"
                             "    Replacements:\n"
                             "      - FilePath:        " +
                             sourcePath.generic_string() +
                             "\n        Offset:          9\n"
                             "        Length:          0\n"
                             "        ReplacementText: '[[nodiscard]] '\n";

    UnifiedReport report;
    Finding finding;
    finding.id = "CS-1";
    finding.file = "src/a.cpp";
    finding.line = 2;
    finding.rule = "modernize-use-nodiscard";
    finding.message = "function 'total' should be marked";
    report.findings.push_back(finding);
    recomputeSummary(report);

    attachFixes(report, yaml, root);

    REQUIRE(report.findings.size() == 1);
    REQUIRE(report.findings[0].fixes.size() == 1);
    REQUIRE(report.findings[0].fixes[0].replacements.size() == 1);
    REQUIRE(report.findings[0].fixes[0].replacements[0].offset == secondLineOffset);
    REQUIRE(report.findings[0].fixes[0].replacements[0].text == "[[nodiscard]] ");
}
