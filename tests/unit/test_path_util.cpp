#include "test_main.hpp"

#include <filesystem>

#include "cppsentinel/process/PathUtil.h"

using namespace cppsentinel;
namespace fs = std::filesystem;

TEST(path_to_msys_drive)
{
    const auto p = toMsysPath(std::filesystem::path("C:/Users/test/a.cpp"));
    REQUIRE(p.find("/c/") == 0 || p.find("/C/") == 0 || p.rfind("/c/", 0) == 0);
    REQUIRE(p.find("Users") != std::string::npos);
}

TEST(path_sanitize_token)
{
    REQUIRE(sanitizeFileToken("clang-tidy") == "clang-tidy");
    REQUIRE(sanitizeFileToken("a/b c") == "a_b_c");
    REQUIRE(sanitizeFileToken("") == "tool");
}

TEST(path_shell_quote)
{
    REQUIRE(shellSingleQuote("abc") == "'abc'");
    REQUIRE(shellSingleQuote("a'b") == "'a'\\''b'");
}

TEST(path_iequals)
{
    REQUIRE(iequals("MINGW64", "mingw64"));
    REQUIRE(!iequals("mingw64", "clang64"));
}

TEST(path_resolve_against)
{
    const auto base = std::filesystem::path("/tmp/cfg");
    const auto rel = resolveAgainst(base, "foo.toml");
    REQUIRE(rel.filename() == "foo.toml");
    const auto abs = resolveAgainst(base, "/abs/x");
    REQUIRE(abs.is_absolute() || abs.generic_string().find("abs") != std::string::npos);
}

TEST(resolve_executable_path_keeps_explicit_paths)
{
    const auto explicitPath = fs::path("C:/tools/bin/clang-tidy.exe");
    REQUIRE(resolveExecutablePath(explicitPath.string()) == explicitPath);

    // A bare name that does not exist on PATH is returned unchanged.
    REQUIRE(resolveExecutablePath("definitely-not-a-real-tool-xyz").string() ==
            "definitely-not-a-real-tool-xyz");
}
