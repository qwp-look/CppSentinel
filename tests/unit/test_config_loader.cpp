#include "test_main.hpp"

#include "cppsentinel/config/ConfigLoader.h"
#include "cppsentinel/util/FileIo.h"

#include <filesystem>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

fs::path writeTempConfig(const std::string& body)
{
    const auto dir = fs::temp_directory_path() / "cppsentinel_cfg_tests";
    fs::create_directories(dir);
    const auto path = dir / "Configuration.toml";
    std::string err;
    REQUIRE(writeTextFile(path, body, err));
    return path;
}

} // namespace

TEST(config_loads_v2_and_preserves_order)
{
    const auto path = writeTempConfig(R"(
schema_version = 2

[project]
root = "."
report_dir = "report"

[run]
fail_on = "error"
timeout_seconds = 30

[environment.native]
kind = "native"

[[checker]]
id = "second"
kind = "cppcheck"
enabled = false

[[checker]]
id = "first"
kind = "clang-format"
enabled = true
)");

    auto loaded = loadConfig(path);
    REQUIRE(loaded.ok);
    REQUIRE(loaded.config.checkers.size() == 2);
    REQUIRE(loaded.config.checkers[0].id == "second");
    REQUIRE(loaded.config.checkers[1].id == "first");
    REQUIRE(loaded.config.run.fail_on == FailOn::Error);
    REQUIRE(loaded.config.run.timeout_seconds.count() == 30);
}

TEST(config_rejects_duplicate_ids)
{
    const auto path = writeTempConfig(R"(
schema_version = 2
[project]
root = "."
[environment.native]
kind = "native"
[[checker]]
id = "a"
kind = "cppcheck"
[[checker]]
id = "a"
kind = "clang-format"
)");
    auto loaded = loadConfig(path);
    REQUIRE(!loaded.ok);
    bool found = false;
    for (const auto& d : loaded.diagnostics)
    {
        if (d.message.find("duplicate") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST(config_rejects_legacy_command)
{
    const auto path = writeTempConfig(R"(
schema_version = 2
[project]
root = "."
[environment.native]
kind = "native"
[[checker]]
id = "x"
kind = "cppcheck"
command = "echo hi"
)");
    auto loaded = loadConfig(path);
    REQUIRE(!loaded.ok);
}

TEST(config_rejects_unknown_top_level)
{
    const auto path = writeTempConfig(R"(
schema_version = 2
[project]
root = "."
[ClangTidy]
enabled = true
)");
    auto loaded = loadConfig(path);
    REQUIRE(!loaded.ok);
}
