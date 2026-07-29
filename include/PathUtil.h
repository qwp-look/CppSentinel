#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace util
{

/// ASCII 大小写不敏感比较（用于 mingw64 / MINGW64 等环境名）。
inline bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
        {
            return false;
        }
    }
    return true;
}

/// 将 Windows/本地路径转为 MSYS2 POSIX 路径。
/// 示例：C:\Users\a\b -> /c/Users/a/b
inline std::string toMsysPath(const std::filesystem::path& input)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path abs = fs::absolute(input, ec);
    if (ec)
    {
        abs = input;
    }
    abs = abs.lexically_normal();

    std::string s = abs.string();
    std::replace(s.begin(), s.end(), '\\', '/');

    // 盘符路径：C:/Users/... -> /c/Users/...
    if (s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) &&
        s[1] == ':')
    {
        const char drive =
            static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        std::string rest = s.substr(2);
        if (rest.empty() || rest.front() != '/')
        {
            rest.insert(rest.begin(), '/');
        }
        return std::string("/") + drive + rest;
    }

    // UNC 或相对路径：保留正斜杠。
    return s;
}

/// 为 bash -c 安全地单引号包裹字符串。
/// 例如 it's 变成 'it'\''s'
inline std::string shellSingleQuote(std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value)
    {
        if (c == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

/// 仅保留适合作为报告文件名的字符。
inline std::string sanitizeFileToken(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.')
        {
            out.push_back(c);
        }
        else
        {
            out.push_back('_');
        }
    }
    if (out.empty())
    {
        out = "tool";
    }
    return out;
}

} // namespace util
