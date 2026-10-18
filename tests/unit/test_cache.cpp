#include "test_main.hpp"

#include "cppsentinel/core/ResultCache.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

fs::path cacheRoot()
{
    return fs::temp_directory_path() / "cppsentinel_cache";
}

void writeSource(const fs::path& path, const std::string& content)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

} // namespace

TEST(result_cache_roundtrips_checker_results)
{
    const auto directory = cacheRoot() / "roundtrip";
    std::error_code ec;
    fs::remove_all(directory, ec);
    ResultCache cache(directory, true);

    CheckerResult original;
    original.checker_id = "clang-tidy";
    original.checker_kind = "clang-tidy";
    original.status = CheckerStatus::Succeeded;
    original.exit_code = 1;
    original.duration = std::chrono::milliseconds(1234);
    original.message = "reported diagnostics";

    Diagnostic primary;
    primary.file = "src/a.cpp";
    primary.line = 12;
    primary.column = 5;
    primary.severity = Severity::Warning;
    primary.rule = "bugprone-x";
    primary.message = "careful";
    original.diagnostics.push_back(primary);

    Diagnostic note;
    note.file = "src/a.cpp";
    note.line = 13;
    note.severity = Severity::Information;
    note.message = "context";
    note.role = DiagnosticRole::Note;
    original.diagnostics.push_back(note);

    cache.store("key1", original);
    const auto loaded = cache.load("key1");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->checker_id == "clang-tidy");
    REQUIRE(loaded->status == CheckerStatus::Succeeded);
    REQUIRE(loaded->exit_code.value_or(0) == 1);
    REQUIRE(loaded->duration.count() == 1234);
    REQUIRE(loaded->diagnostics.size() == 2);
    REQUIRE(loaded->diagnostics[0].line.value_or(0) == 12);
    REQUIRE(loaded->diagnostics[0].severity == Severity::Warning);
    REQUIRE(loaded->diagnostics[1].role == DiagnosticRole::Note);

    REQUIRE(!cache.load("missing").has_value());
}

TEST(disabled_cache_never_stores)
{
    const auto directory = cacheRoot() / "disabled";
    std::error_code ec;
    fs::remove_all(directory, ec);
    ResultCache cache(directory, false);

    CheckerResult result;
    result.checker_id = "x";
    cache.store("key", result);
    REQUIRE(!cache.load("key").has_value());
    REQUIRE(!fs::exists(directory / "key.json"));
}

TEST(project_fingerprint_changes_with_content)
{
    const auto root = cacheRoot() / "fingerprint";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeSource(root / "src" / "a.cpp", "int a;\n");

    const auto first = projectFingerprint(root);
    REQUIRE(!first.empty());
    REQUIRE(projectFingerprint(root) == first); // stable across calls

    writeSource(root / "src" / "a.cpp", "int a;\nint b;\n");
    const auto second = projectFingerprint(root);
    REQUIRE(second != first);

    writeSource(root / "src" / "b.cpp", "int c;\n");
    const auto third = projectFingerprint(root);
    REQUIRE(third != second);
}

TEST(project_fingerprint_ignores_build_output)
{
    const auto root = cacheRoot() / "fingerprint-build";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeSource(root / "src" / "a.cpp", "int a;\n");

    const auto before = projectFingerprint(root);
    writeSource(root / "build" / "generated.cpp", "int generated;\n");
    REQUIRE(projectFingerprint(root) == before);
}

TEST(cache_key_depends_on_options_and_version)
{
    const ResultCache cache(cacheRoot(), true);
    CheckerConfig checker;
    checker.id = "tidy";
    checker.kind = "clang-tidy";
    checker.options["checks"] = "bugprone-*";

    const auto base = cache.keyFor(checker, "version-a", "fingerprint");
    REQUIRE(base == cache.keyFor(checker, "version-a", "fingerprint"));
    REQUIRE(base != cache.keyFor(checker, "version-b", "fingerprint"));
    REQUIRE(base != cache.keyFor(checker, "version-a", "other-fingerprint"));

    checker.options["checks"] = "modernize-*";
    REQUIRE(base != cache.keyFor(checker, "version-a", "fingerprint"));
}
