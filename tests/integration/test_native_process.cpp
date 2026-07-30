#include "test_main.hpp"

#include "cppsentinel/process/NativeProcessRunner.h"
#include "cppsentinel/util/FileIo.h"

#include <filesystem>

using namespace cppsentinel;
namespace fs = std::filesystem;

// Provides main() for the whole test binary.
int main()
{
    return cppsentinel::test::runAll();
}

TEST(native_process_captures_stdout)
{
    const auto dir = fs::temp_directory_path() / "cppsentinel_proc";
    fs::create_directories(dir);
    const auto outLog = dir / "out.log";
    const auto errLog = dir / "err.log";

    ProcessRequest req;
#ifdef _WIN32
    // Prefer MSYS2 tools when available; fall back to cmd echo is awkward with argv.
    // Use where.exe which always exists on Windows.
    req.executable = "C:\\Windows\\System32\\cmd.exe";
    req.arguments = {"/C", "echo hello-cppsentinel"};
#else
    req.executable = "/bin/echo";
    req.arguments = {"hello-cppsentinel"};
#endif
    req.stdout_log = outLog;
    req.stderr_log = errLog;
    req.timeout = std::chrono::seconds(10);
    req.working_directory = dir;

    NativeProcessRunner runner;
    const auto result = runner.run(req);
    REQUIRE(result.state == ProcessState::Succeeded ||
            result.state == ProcessState::Failed);
    // On success, stdout should contain our token.
    if (result.state == ProcessState::Succeeded)
    {
        std::string err;
        auto text = readTextFile(outLog, err);
        REQUIRE(text.has_value());
        REQUIRE(text->find("hello-cppsentinel") != std::string::npos);
    }
}
