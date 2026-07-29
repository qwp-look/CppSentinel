// Scheduler.cpp
// 依次运行配置中的 Method，并汇总启动/退出结果。

#include "Scheduler.h"

#include "CommandBuilder.h"
#include "Msys2Runner.h"
#include "ReportDir.h"

#include <iostream>

namespace util
{

ScheduleSummary scheduleMethods(const std::vector<Method>& methods,
                                const SchedulePaths& paths)
{
    ScheduleSummary summary;
    summary.total = static_cast<int>(methods.size());

    // 本轮只定位一次 MSYS2，供所有 method 复用。
    std::string msysError;
    const auto msysInstall = findMsys2Install(msysError);
    if (!msysInstall)
    {
        std::cerr << "[scheduler] " << msysError << '\n';
    }

    const std::string checkPathStr = paths.checkPath.string();

    for (const auto& method : methods)
    {
        std::cout << "\n--- " << method.name << " ---\n";

        if (!method.enabled)
        {
            ++summary.skipped;
            std::cout << "已跳过（未启用）\n";
            continue;
        }

        if (method.command.empty())
        {
            ++summary.failed;
            std::cerr << "错误: 已启用的 method 命令为空\n";
            continue;
        }

        const auto env = parseMsysEnvironment(method.environment);
        if (!env)
        {
            ++summary.failed;
            std::cerr << "错误: 不支持的 environment '" << method.environment
                      << "'\n";
            continue;
        }

        if (!msysInstall)
        {
            ++summary.failed;
            std::cerr << "错误: 无法使用 MSYS2，跳过 " << method.name << '\n';
            continue;
        }

        // 同一报告目录下，每个 method 对应一个输出文件。
        const auto outputPath =
            makeMethodOutputPath(paths.reportDir, method.name);
        const std::string command =
            buildCommand(method, checkPathStr, outputPath.string(), true);

        std::cout << "环境       : " << msysEnvironmentName(*env) << '\n';
        std::cout << "输出文件   : " << outputPath.string() << '\n';
        std::cout << "命令       : " << command << '\n';

        // 此处失败不中断循环，后续 method 仍会执行。
        const RunResult result = runInMsys2(*env, command, msysInstall);
        if (result.launched)
        {
            ++summary.launched;
            if (result.exit_code == 0)
            {
                ++summary.succeeded;
            }
            else
            {
                ++summary.failed;
            }
        }
        else
        {
            ++summary.failed;
        }

        std::cout << "已启动     : " << (result.launched ? "是" : "否") << '\n';
        std::cout << "退出码     : " << result.exit_code << '\n';
        std::cout << "说明       : " << result.message << '\n';
    }

    return summary;
}

} // namespace util
