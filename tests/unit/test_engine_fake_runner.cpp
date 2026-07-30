#include "test_main.hpp"

#include "cppsentinel/core/Engine.h"
#include "cppsentinel/process/FakeProcessRunner.h"

#include <filesystem>
#include <fstream>

using namespace cppsentinel;
namespace fs = std::filesystem;

TEST(engine_runs_with_fake_process_runner)
{
    AppConfig config;
    config.schema_version = 2;
    config.config_path = "test.toml";
    config.project.root = fs::temp_directory_path() / "cppsentinel_engine";
    config.project.report_dir = config.project.root / "report";
    fs::create_directories(config.project.root);

    EnvironmentProfile native;
    native.name = "native";
    native.kind = EnvironmentKind::Native;
    config.environments["native"] = native;

    CheckerConfig checker;
    checker.id = "fmt";
    checker.kind = "clang-format";
    checker.enabled = true;
    checker.environment = "native";
    config.checkers.push_back(checker);

    CheckerConfig disabled;
    disabled.id = "off";
    disabled.kind = "cppcheck";
    disabled.enabled = false;
    disabled.environment = "native";
    config.checkers.push_back(disabled);

    auto fake = std::make_shared<FakeProcessRunner>();
    ProcessResult ok;
    ok.state = ProcessState::Succeeded;
    ok.exit_code = 0;
    ok.duration = std::chrono::milliseconds(1);
    fake->enqueue(ok);

    CheckerRegistry registry;
    registerBuiltinCheckers(registry);
    Engine engine(std::move(registry));

    RunRequest req;
    req.config = config;
    req.target = config.project.root / "dummy.cpp";
    {
        std::ofstream out(req.target);
        out << "int main(){return 0;}\n";
    }
    req.default_runner = fake;

    const auto report = engine.run(req);
    REQUIRE(report.checkers.size() == 2);
    REQUIRE(report.checkers[0].checker_id == "fmt");
    REQUIRE(report.checkers[1].status == CheckerStatus::Skipped);
    REQUIRE(fs::exists(report.metadata.report_dir / "report.json"));
    REQUIRE(!fake->requests().empty());
}

TEST(engine_unknown_kind_is_config_error)
{
    AppConfig config;
    config.project.root = fs::temp_directory_path() / "cppsentinel_engine2";
    config.project.report_dir = config.project.root / "report";
    fs::create_directories(config.project.root);

    EnvironmentProfile native;
    native.name = "native";
    native.kind = EnvironmentKind::Native;
    config.environments["native"] = native;

    CheckerConfig checker;
    checker.id = "x";
    checker.kind = "not-a-real-checker";
    checker.enabled = true;
    checker.environment = "native";
    config.checkers.push_back(checker);

    CheckerRegistry registry;
    registerBuiltinCheckers(registry);
    Engine engine(std::move(registry));

    RunRequest req;
    req.config = config;
    req.target = config.project.root / "a.cpp";
    {
        std::ofstream out(req.target);
        out << "int main(){}\n";
    }

    const auto report = engine.run(req);
    REQUIRE(report.checkers.size() == 1);
    REQUIRE(report.checkers[0].status == CheckerStatus::ConfigError);
    REQUIRE(report.exit_code == 2);
}
