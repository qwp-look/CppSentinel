// MSYS2 runner: locate bash.exe, set MSYSTEM, exec tool via `exec "$@"`.

#include "cppsentinel/process/Msys2ProcessRunner.h"

#include "cppsentinel/process/Environment.h"
#include "cppsentinel/process/PathUtil.h"
#include "cppsentinel/process/ProcessLaunch.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;

std::optional<std::string> getEnvVar(const char* name)
{
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0')
        {
            return std::string(value);
        }
    }
    return std::nullopt;
}

bool looksLikeMsysRoot(const fs::path& root)
{
    std::error_code ec;
    const fs::path bash = root / "usr" / "bin" / "bash.exe";
    return fs::is_regular_file(bash, ec) && !ec;
}

// MSYS2's runtime rewrites argv entries that look like POSIX paths before
// handing them to native Windows programs. Converting "C:/x/y" to "/c/x/y"
// ourselves therefore mangles every path twice ("C:/c/x/y") and also corrupts
// flags such as "--export-fixes=C:/...". The checkers drive native tools, so we
// pass native paths verbatim and disable the runtime conversion instead.
constexpr const char* kArgConvExcl = "MSYS2_ARG_CONV_EXCL";

} // namespace

std::optional<Msys2Install> findMsys2Install(const EnvironmentProfile* profile,
                                             std::string& errorMessage)
{
    std::vector<fs::path> candidates;
    if (profile && !profile->root.empty())
    {
        candidates.push_back(profile->root);
    }
    if (auto root = getEnvVar("MSYS2_ROOT"))
    {
        candidates.emplace_back(*root);
    }
    if (auto root = getEnvVar("MSYS2_PATH"))
    {
        candidates.emplace_back(*root);
    }

    static const std::array<fs::path, 6> defaults = {
        R"(C:\msys64)",       R"(D:\msys64)", R"(E:\msys64)",
        R"(C:\tools\msys64)", R"(C:\msys2)",  R"(D:\msys2)",
    };
    candidates.insert(candidates.end(), defaults.begin(), defaults.end());

    for (const auto& root : candidates)
    {
        if (root.empty())
        {
            continue;
        }
        if (looksLikeMsysRoot(root))
        {
            Msys2Install install;
            install.root = root;
            install.bash = root / "usr" / "bin" / "bash.exe";
            return install;
        }
    }

    errorMessage =
        "MSYS2 installation not found. Set environment.root or MSYS2_ROOT "
        "(expected <root>/usr/bin/bash.exe).";
    return std::nullopt;
}

Msys2ProcessRunner::Msys2ProcessRunner(EnvironmentProfile profile)
    : profile_(std::move(profile))
{
}

ProcessResult Msys2ProcessRunner::run(const ProcessRequest& request) const
{
#ifndef _WIN32
    ProcessResult result;
    result.state = ProcessState::StartError;
    result.error_message = "Msys2ProcessRunner is only supported on Windows";
    result.stdout_log = request.stdout_log;
    result.stderr_log = request.stderr_log;
    return result;
#else
    std::string locateError;
    auto install = findMsys2Install(&profile_, locateError);
    if (!install)
    {
        ProcessResult result;
        result.state = ProcessState::StartError;
        result.error_message = locateError;
        result.stdout_log = request.stdout_log;
        result.stderr_log = request.stderr_log;
        return result;
    }

    // bash -l -c 'exec "$@"' cppsentinel-tool <exe> <args...>
    // argv after -c script become $0, $1, ... so exec "$@" re-execs without shell
    // word-splitting on tool arguments.
    std::vector<std::string> bashArgs;
    bashArgs.emplace_back("-l");
    bashArgs.emplace_back("-c");
    bashArgs.emplace_back("exec \"$@\"");
    bashArgs.emplace_back("cppsentinel-tool");

    const std::string exe = request.executable.string();
    bashArgs.push_back(exe);
    for (const auto& arg : request.arguments)
    {
        bashArgs.push_back(arg);
    }

    // Keep every argument byte-for-byte: no POSIX rewriting, no MSYS2 conversion.
    std::map<std::string, std::string> extraEnv = request.environment;
    extraEnv[kArgConvExcl] = "*";
    auto env = buildEnvironmentBlock(request.inherit_parent_env, &profile_,
                                     extraEnv);

    fs::path cwd = request.working_directory;
    // Keep working directory as native path for CreateProcess start dir.

    return launchProcess(install->bash, bashArgs, cwd, env, request.timeout,
                         request.stdout_log, request.stderr_log);
#endif
}

} // namespace cppsentinel
