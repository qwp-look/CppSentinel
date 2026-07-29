// Msys2Runner.cpp
// 在 MSYS2 登录 shell 中启动审计工具命令。
// 使用内嵌 Boost.Process v2（third_party/boost）与 std::filesystem。

#include "Msys2Runner.h"

#include "PathUtil.h"

#include <boost/asio/io_context.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/process.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace util
{
namespace
{

namespace bp = boost::process::v2;
namespace fs = std::filesystem;

// 读取当前进程的可选环境变量。
std::optional<std::string> getEnvVar(const char* name)
{
    // 可移植 getenv；MSVC 与 MinGW 运行时均可用。
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0')
        {
            return std::string(value);
        }
    }
    return std::nullopt;
}

// 当 <root>/usr/bin/bash.exe 存在时视为有效 MSYS2 根。
bool looksLikeMsysRoot(const fs::path& root)
{
    std::error_code ec;
    const fs::path bash = root / "usr" / "bin" / "bash.exe";
    return fs::is_regular_file(bash, ec) && !ec;
}

// 构造完整子进程环境：拷贝父进程，再强制设置 MSYS2 选择器。
// process_environment 会替换整个环境块，只传少量变量会丢掉 PATH/SystemRoot。
std::vector<std::string> buildChildEnvironment(std::string_view msystem)
{
    std::vector<std::string> env;
    env.reserve(128);

    for (auto entry : bp::environment::current())
    {
        const std::string key = entry.key().string();
        if (iequals(key, "MSYSTEM") || iequals(key, "CHERE_INVOKING") ||
            iequals(key, "MSYS2_PATH_TYPE"))
        {
            continue;
        }
        env.push_back(entry.string());
    }

    env.emplace_back(std::string("MSYSTEM=") + std::string(msystem));
    env.emplace_back("CHERE_INVOKING=1");
    env.emplace_back("MSYS2_PATH_TYPE=inherit");
    return env;
}

} // namespace

std::optional<MsysEnvironment> parseMsysEnvironment(std::string_view name)
{
    if (iequals(name, "mingw64"))
    {
        return MsysEnvironment::Mingw64;
    }
    if (iequals(name, "clang64"))
    {
        return MsysEnvironment::Clang64;
    }
    if (iequals(name, "ucrt64"))
    {
        return MsysEnvironment::Ucrt64;
    }
    if (iequals(name, "clangarm64"))
    {
        return MsysEnvironment::Clangarm64;
    }
    if (iequals(name, "msys") || iequals(name, "msys2"))
    {
        return MsysEnvironment::Msys;
    }
    return std::nullopt;
}

std::string_view msysEnvironmentName(MsysEnvironment env)
{
    switch (env)
    {
    case MsysEnvironment::Mingw64:
        return "mingw64";
    case MsysEnvironment::Clang64:
        return "clang64";
    case MsysEnvironment::Ucrt64:
        return "ucrt64";
    case MsysEnvironment::Clangarm64:
        return "clangarm64";
    case MsysEnvironment::Msys:
        return "msys";
    }
    return "unknown";
}

std::string_view msystemValue(MsysEnvironment env)
{
    switch (env)
    {
    case MsysEnvironment::Mingw64:
        return "MINGW64";
    case MsysEnvironment::Clang64:
        return "CLANG64";
    case MsysEnvironment::Ucrt64:
        return "UCRT64";
    case MsysEnvironment::Clangarm64:
        return "CLANGARM64";
    case MsysEnvironment::Msys:
        return "MSYS";
    }
    return "MSYS";
}

std::optional<Msys2Install> findMsys2Install(std::string& errorMessage)
{
    std::vector<fs::path> candidates;

    if (auto root = getEnvVar("MSYS2_ROOT"))
    {
        candidates.emplace_back(*root);
    }
    if (auto root = getEnvVar("MSYS2_PATH"))
    {
        candidates.emplace_back(*root);
    }

    // Windows 开发机上常见的默认安装位置。
    static const std::array<fs::path, 6> defaults = {
        R"(C:\msys64)",       R"(D:\msys64)", R"(E:\msys64)",
        R"(C:\tools\msys64)", R"(C:\msys2)",  R"(D:\msys2)",
    };
    std::copy(defaults.begin(), defaults.end(), std::back_inserter(candidates));

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

    errorMessage = "未找到 MSYS2 安装。请设置 MSYS2_ROOT 为安装根目录"
                   "（期望存在 <root>/usr/bin/bash.exe）。";
    return std::nullopt;
}

RunResult runInMsys2(MsysEnvironment env, std::string_view command,
                     const std::optional<Msys2Install>& install)
{
    if (command.empty())
    {
        return RunResult::failure("拒绝执行空命令");
    }

    Msys2Install resolved;
    if (install.has_value())
    {
        resolved = *install;
    }
    else
    {
        std::string locateError;
        auto found = findMsys2Install(locateError);
        if (!found)
        {
            return RunResult::failure(locateError);
        }
        resolved = *found;
    }

    std::error_code ec;
    if (!fs::is_regular_file(resolved.bash, ec) || ec)
    {
        return RunResult::failure("未找到 bash.exe: " + resolved.bash.string());
    }

    try
    {
        boost::asio::io_context ctx;
        auto childEnv = buildChildEnvironment(msystemValue(env));
        bp::process_environment processEnv(childEnv);

        // 定义了 BOOST_PROCESS_USE_STD_FS 后，Process v2 接受
        // std::filesystem::path。
        const fs::path bashPath = resolved.bash;
        const std::string commandText(command);
        const std::vector<std::string> args{"-l", "-c", commandText};

        // 直接传 bash argv，避免 cmd.exe / msys2_shell.cmd 嵌套引号。
        bp::process proc(ctx, bashPath, args, processEnv);

        proc.wait();
        const int code = proc.exit_code();

        RunResult result = RunResult::success(code);
        if (code != 0)
        {
            result.message = std::string("命令退出码 ") + std::to_string(code) +
                             "（环境 " + std::string(msysEnvironmentName(env)) +
                             "）";
        }
        else
        {
            result.message = std::string("成功（") +
                             std::string(msysEnvironmentName(env)) + "）";
        }
        return result;
    }
    catch (const std::system_error& e)
    {
        return RunResult::failure(std::string("启动 MSYS2 失败（") +
                                      std::string(msysEnvironmentName(env)) +
                                      "）: " + e.what(),
                                  e.code().value());
    }
    catch (const std::exception& e)
    {
        return RunResult::failure(std::string("启动 MSYS2 失败（") +
                                  std::string(msysEnvironmentName(env)) +
                                  "）: " + e.what());
    }
}

} // namespace util
