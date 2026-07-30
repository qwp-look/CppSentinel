// Path conversion, shell quoting, and report filename sanitization.

#include "cppsentinel/process/PathUtil.h"

#include <algorithm>
#include <cctype>

namespace cppsentinel
{

bool iequals(std::string_view a, std::string_view b)
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

std::string toMsysPath(const std::filesystem::path& input)
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
    return s;
}

std::string shellSingleQuote(std::string_view value)
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

std::string sanitizeFileToken(std::string_view name)
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

std::filesystem::path resolveAgainst(const std::filesystem::path& baseDir,
                                     const std::filesystem::path& maybeRelative)
{
    if (maybeRelative.empty())
    {
        return {};
    }
    if (maybeRelative.is_absolute())
    {
        return maybeRelative.lexically_normal();
    }
    return (baseDir / maybeRelative).lexically_normal();
}

} // namespace cppsentinel
