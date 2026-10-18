// Path conversion, shell quoting, and report filename sanitization.

#include "cppsentinel/process/PathUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

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

std::filesystem::path resolveExecutablePath(std::string_view name)
{
    if (name.empty())
    {
        return {};
    }

    const std::string text(name);
    // Explicit path (absolute, contains a separator, or a drive prefix).
    if (text.find('/') != std::string::npos ||
        text.find('\\') != std::string::npos ||
        (text.size() > 1 && text[1] == ':'))
    {
        return std::filesystem::path(text);
    }

    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr)
    {
        return std::filesystem::path(text);
    }

    std::string pathValue(pathEnv);
    // Windows PATH uses ';'; MSYS shells export a ':' separated list.
    const char separator = pathValue.find(';') != std::string::npos ? ';' : ':';

    static const char* kExtensions[] = {"", ".exe", ".cmd", ".bat", ".com"};

    std::size_t start = 0;
    while (start <= pathValue.size())
    {
        auto end = pathValue.find(separator, start);
        if (end == std::string::npos)
        {
            end = pathValue.size();
        }
        std::string directory = pathValue.substr(start, end - start);
        start = end + 1;

        if (directory.empty())
        {
            continue;
        }

        // Convert MSYS-style "/c/foo" entries to "C:/foo".
        if (directory.size() >= 3 && directory[0] == '/' &&
            std::isalpha(static_cast<unsigned char>(directory[1])) &&
            directory[2] == '/')
        {
            std::string converted;
            converted.push_back(directory[1]);
            converted.push_back(':');
            converted += directory.substr(2);
            directory = std::move(converted);
        }

        for (const char* extension : kExtensions)
        {
            const std::filesystem::path candidate =
                std::filesystem::path(directory) / (text + extension);
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec) && !ec)
            {
                return candidate;
            }
        }
    }

    return std::filesystem::path(text);
}

} // namespace cppsentinel
