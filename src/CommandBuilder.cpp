// CommandBuilder.cpp
// 展开 method 命令模板中的 {file_in}/{file_out}。

#include "CommandBuilder.h"

#include "PathUtil.h"

#include <filesystem>

namespace util
{
namespace
{

void replaceAll(std::string& str, const std::string& from,
                const std::string& to)
{
    if (from.empty())
    {
        return;
    }

    std::size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos)
    {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// 命令将在 bash 下执行时，把路径转为 MSYS POSIX 形式。
std::string preparePathToken(const std::string& rawPath, bool forMsys)
{
    if (!forMsys)
    {
        return rawPath;
    }
    return toMsysPath(std::filesystem::path(rawPath));
}

} // namespace

std::string buildCommand(const Method& method, const std::string& fileIn,
                         const std::string& fileOut, bool forMsys)
{
    std::string command = method.command;
    const std::string inToken = preparePathToken(fileIn, forMsys);
    const std::string outToken = preparePathToken(fileOut, forMsys);

    replaceAll(command, "{file_in}", inToken);
    replaceAll(command, "{file_out}", outToken);
    return command;
}

} // namespace util
