// Native runner: direct CreateProcess / posix_spawn style launch via Boost.Process.

#include "cppsentinel/process/NativeProcessRunner.h"

#include "cppsentinel/process/Environment.h"
#include "cppsentinel/process/ProcessLaunch.h"

namespace cppsentinel
{

ProcessResult NativeProcessRunner::run(const ProcessRequest& request) const
{
    const auto env =
        buildEnvironmentBlock(request.inherit_parent_env, nullptr, request.environment);
    return launchProcess(request.executable, request.arguments,
                         request.working_directory, env, request.timeout,
                         request.stdout_log, request.stderr_log);
}

} // namespace cppsentinel
