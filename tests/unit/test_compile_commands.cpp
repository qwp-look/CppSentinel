#include "test_main.hpp"

#include "cppsentinel/checker/CompileCommands.h"
#include "cppsentinel/util/FileIo.h"

#include <filesystem>

using namespace cppsentinel;
namespace fs = std::filesystem;

TEST(compile_commands_load_and_syntax_only)
{
    const auto dir = fs::temp_directory_path() / "cppsentinel_cdb";
    fs::create_directories(dir);
    const auto cdb = dir / "compile_commands.json";
    const auto body = std::string(R"([
  {
    "directory": ")") +
                      dir.generic_string() + R"(",
    "command": "g++ -std=c++20 -Iinclude -c main.cpp -o main.o",
    "file": "main.cpp"
  }
])";
    std::string err;
    REQUIRE(writeTextFile(cdb, body, err));

    auto entries = loadCompileCommands(cdb, err);
    REQUIRE(!entries.empty());
    const auto found =
        REQUIRE_VALUE(findCompileCommand(entries, dir / "main.cpp"));

    auto args = syntaxOnlyArgsFromCompileCommand(found, dir / "main.cpp");
    bool hasSyntax = false;
    bool hasO = false;
    for (const auto& a : args)
    {
        if (a == "-fsyntax-only")
            hasSyntax = true;
        if (a == "-o")
            hasO = true;
    }
    REQUIRE(hasSyntax);
    REQUIRE(!hasO);
}

TEST(split_command_line_quotes)
{
    auto args = splitCommandLine(R"(clang++ -DFOO="a b" "my file.cpp")");
    REQUIRE(args.size() >= 3);
    REQUIRE(args.back() == "my file.cpp");
}
