// CppSentinel 程序入口。
// 编排流程：查找配置 -> 解析 method -> 创建报告目录 -> 调度执行。

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "FileFinder.h"
#include "ReportDir.h"
#include "Scheduler.h"
#include "TomlParser.h"

namespace
{

void printUsage(const char* argv0)
{
    std::cerr
        << "用法:\n"
        << "  " << argv0 << " <search_path> <check_path>\n"
        << "  " << argv0 << '\n'
        << "\n"
        << "参数:\n"
        << "  search_path   用于搜索 Configuration.toml 的目录\n"
        << "  check_path    替换到 {file_in} 的路径\n"
        << "\n"
        << "环境变量:\n"
        << "  MSYS2_ROOT    可选；MSYS2 安装根目录（含 usr/bin/bash.exe）\n"
        << "\n"
        << "示例:\n"
        << "  " << argv0 << " \".\\Configuration\" \".\\main.cpp\"\n";
}

bool readInteractivePaths(std::string& searchPath, std::string& checkPath)
{
    std::cout << "请输入 Configuration.toml 的搜索路径: " << std::flush;
    if (!std::getline(std::cin, searchPath) || searchPath.empty())
    {
        std::cerr << "错误: 搜索路径为空\n";
        return false;
    }

    std::cout << "请输入 {file_in} 对应的检查路径: " << std::flush;
    if (!std::getline(std::cin, checkPath) || checkPath.empty())
    {
        std::cerr << "错误: 检查路径为空\n";
        return false;
    }
    return true;
}

int run(int argc, char* argv[])
{
    std::string searchPath;
    std::string checkPath;
    const bool interactive = (argc == 1);

    // --- 参数解析 ---------------------------------------------------------
    if (argc == 3)
    {
        searchPath = argv[1];
        checkPath = argv[2];
    }
    else if (interactive)
    {
        if (!readInteractivePaths(searchPath, checkPath))
        {
            return 1;
        }
    }
    else
    {
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "\n=== CppSentinel ===\n";
    std::cout << "搜索路径 : " << searchPath << '\n';
    std::cout << "检查路径 : " << checkPath << '\n';

    // --- 定位 Configuration.toml ------------------------------------------
    // 命令行模式失败即退出；交互模式允许重试搜索路径。
    std::filesystem::path configPath;
    while (true)
    {
        std::string findError;
        auto found = util::findConfigurationFile(searchPath, findError);
        if (found)
        {
            configPath = found->path;
            std::cout << "配置文件 : " << configPath.string() << '\n';
            break;
        }

        std::cerr << "错误: " << findError << '\n';
        if (!interactive)
        {
            return 1;
        }

        std::cout << "请重新输入搜索路径（空行退出）: " << std::flush;
        if (!std::getline(std::cin, searchPath) || searchPath.empty())
        {
            return 1;
        }
    }

    // --- 解析 method ------------------------------------------------------
    std::vector<Method> methods;
    std::string parseError;
    if (!util::parseMethodConfiguration(configPath, methods, parseError))
    {
        std::cerr << "错误: " << parseError << '\n';
        return 1;
    }
    std::cout << "Method 数 : " << methods.size() << '\n';

    // --- 创建 report/<时间戳>/ ---------------------------------------------
    std::string reportError;
    auto report = util::createTimestampedReportDir("report", reportError);
    if (!report)
    {
        std::cerr << "错误: " << reportError << '\n';
        return 1;
    }
    std::cout << "报告目录 : " << report->path.string() << '\n';

    // --- 执行全部已启用 method --------------------------------------------
    const util::SchedulePaths paths{
        .checkPath = std::filesystem::path(checkPath),
        .reportDir = report->path,
    };
    const auto summary = util::scheduleMethods(methods, paths);

    std::cout << "\n=== 汇总 ===\n";
    std::cout << "总数     : " << summary.total << '\n';
    std::cout << "跳过     : " << summary.skipped << '\n';
    std::cout << "已启动   : " << summary.launched << '\n';
    std::cout << "成功     : " << summary.succeeded << '\n';
    std::cout << "失败     : " << summary.failed << '\n';

    // 0 = 全部已启用 method 成功；2 = 存在失败项。
    return summary.failed == 0 ? 0 : 2;
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "未处理异常: " << ex.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "未处理的未知异常\n";
        return 1;
    }
}
