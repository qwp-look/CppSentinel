// Boost.Process v2 launch: redirect stdio to log files, async wait + timeout.

#include "cppsentinel/process/ProcessLaunch.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/stdio.hpp>

#include <chrono>
#include <fstream>
#include <utility>

namespace cppsentinel
{
namespace
{

namespace bp = boost::process::v2;
namespace fs = std::filesystem;

void ensureParent(const fs::path& file)
{
    const auto parent = file.parent_path();
    if (parent.empty())
    {
        return;
    }
    std::error_code ec;
    fs::create_directories(parent, ec);
}

void touchEmpty(const fs::path& file)
{
    ensureParent(file);
    std::ofstream out(file, std::ios::out | std::ios::trunc | std::ios::binary);
}

} // namespace

ProcessResult launchProcess(const fs::path& executable,
                            const std::vector<std::string>& arguments,
                            const fs::path& workingDirectory,
                            const std::vector<std::string>& environment,
                            std::chrono::seconds timeout,
                            const fs::path& stdoutLog,
                            const fs::path& stderrLog)
{
    ProcessResult result;
    result.stdout_log = stdoutLog;
    result.stderr_log = stderrLog;

    if (executable.empty())
    {
        result.state = ProcessState::StartError;
        result.error_message = "executable path is empty";
        return result;
    }

    ensureParent(stdoutLog);
    ensureParent(stderrLog);
    touchEmpty(stdoutLog);
    touchEmpty(stderrLog);

    const auto started = std::chrono::steady_clock::now();

    try
    {
        boost::asio::io_context ctx;
        bp::process_environment processEnv(environment);

        // Redirect child stdout/stderr straight into log files to avoid pipe
        // buffer deadlocks and keep raw logs even when tools crash.
        bp::process_stdio stdio{nullptr, stdoutLog, stderrLog};

        std::unique_ptr<bp::process> proc;
        if (!workingDirectory.empty())
        {
            proc = std::make_unique<bp::process>(
                ctx, executable, arguments, processEnv, stdio,
                bp::process_start_dir{workingDirectory});
        }
        else
        {
            proc = std::make_unique<bp::process>(ctx, executable, arguments,
                                                 processEnv, stdio);
        }

        bool timedOut = false;
        int exitCode = -1;
        boost::system::error_code waitEc;

        boost::asio::steady_timer timer(ctx);
        if (timeout.count() > 0)
        {
            timer.expires_after(timeout);
            timer.async_wait(
                [&](const boost::system::error_code& ec)
                {
                    if (ec)
                    {
                        return;
                    }
                    timedOut = true;
                    boost::system::error_code termEc;
                    proc->terminate(termEc);
                });
        }

        proc->async_wait(
            [&](const boost::system::error_code& ec, int code)
            {
                waitEc = ec;
                exitCode = code;
                timer.cancel();
            });

        ctx.run();

        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        if (timedOut)
        {
            result.state = ProcessState::TimedOut;
            result.exit_code = exitCode;
            result.error_message = "process timed out after " +
                                   std::to_string(timeout.count()) + "s";
            return result;
        }

        if (waitEc)
        {
            result.state = ProcessState::StartError;
            result.error_message = "wait failed: " + waitEc.message();
            return result;
        }

        result.exit_code = exitCode;
        if (exitCode == 0)
        {
            result.state = ProcessState::Succeeded;
        }
        else
        {
            result.state = ProcessState::Failed;
            result.error_message =
                "process exited with code " + std::to_string(exitCode);
        }
        return result;
    }
    catch (const std::system_error& e)
    {
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        result.state = ProcessState::StartError;
        result.error_message =
            std::string("failed to start process: ") + e.what();
        return result;
    }
    catch (const std::exception& e)
    {
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        result.state = ProcessState::StartError;
        result.error_message =
            std::string("failed to start process: ") + e.what();
        return result;
    }
}

} // namespace cppsentinel
