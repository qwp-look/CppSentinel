// Minimal compile_commands.json reader for -p / syntax-only argument recovery.

#include "cppsentinel/checker/CompileCommands.h"

#include "cppsentinel/util/FileIo.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;

std::string_view trimView(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::optional<std::string> extractJsonString(std::string_view object,
                                             std::string_view key)
{
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    std::size_t pos = 0;
    while (true)
    {
        pos = object.find(pattern, pos);
        if (pos == std::string_view::npos)
        {
            return std::nullopt;
        }
        // ensure key boundary
        std::size_t valuePos = pos + pattern.size();
        while (valuePos < object.size() &&
               std::isspace(static_cast<unsigned char>(object[valuePos])))
        {
            ++valuePos;
        }
        if (valuePos >= object.size() || object[valuePos] != ':')
        {
            pos += pattern.size();
            continue;
        }
        ++valuePos;
        while (valuePos < object.size() &&
               std::isspace(static_cast<unsigned char>(object[valuePos])))
        {
            ++valuePos;
        }
        if (valuePos >= object.size() || object[valuePos] != '"')
        {
            return std::nullopt;
        }
        ++valuePos;
        std::string out;
        while (valuePos < object.size())
        {
            char c = object[valuePos++];
            if (c == '\\' && valuePos < object.size())
            {
                char n = object[valuePos++];
                if (n == 'n')
                    out.push_back('\n');
                else if (n == 't')
                    out.push_back('\t');
                else if (n == 'r')
                    out.push_back('\r');
                else
                    out.push_back(n);
                continue;
            }
            if (c == '"')
            {
                break;
            }
            out.push_back(c);
        }
        return out;
    }
}

std::optional<std::vector<std::string>>
extractJsonStringArray(std::string_view object, std::string_view key)
{
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    auto pos = object.find(pattern);
    if (pos == std::string_view::npos)
    {
        return std::nullopt;
    }
    pos = object.find(':', pos + pattern.size());
    if (pos == std::string_view::npos)
    {
        return std::nullopt;
    }
    ++pos;
    while (pos < object.size() &&
           std::isspace(static_cast<unsigned char>(object[pos])))
    {
        ++pos;
    }
    if (pos >= object.size() || object[pos] != '[')
    {
        return std::nullopt;
    }
    ++pos;
    std::vector<std::string> values;
    while (pos < object.size())
    {
        while (pos < object.size() &&
               (std::isspace(static_cast<unsigned char>(object[pos])) ||
                object[pos] == ','))
        {
            ++pos;
        }
        if (pos < object.size() && object[pos] == ']')
        {
            break;
        }
        if (pos >= object.size() || object[pos] != '"')
        {
            break;
        }
        ++pos;
        std::string item;
        while (pos < object.size())
        {
            char c = object[pos++];
            if (c == '\\' && pos < object.size())
            {
                item.push_back(object[pos++]);
                continue;
            }
            if (c == '"')
            {
                break;
            }
            item.push_back(c);
        }
        values.push_back(std::move(item));
    }
    return values;
}

std::vector<std::string_view> splitObjects(std::string_view json)
{
    std::vector<std::string_view> objects;
    int depth = 0;
    std::size_t start = 0;
    bool inString = false;
    bool escape = false;
    for (std::size_t i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if (inString)
        {
            if (escape)
            {
                escape = false;
            }
            else if (c == '\\')
            {
                escape = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }
        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '{')
        {
            if (depth == 0)
            {
                start = i;
            }
            ++depth;
        }
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                objects.push_back(json.substr(start, i - start + 1));
            }
        }
    }
    return objects;
}

fs::path normalizePath(const fs::path& p)
{
    std::error_code ec;
    auto abs = fs::weakly_canonical(p, ec);
    if (ec)
    {
        abs = fs::absolute(p, ec);
        if (ec)
        {
            abs = p;
        }
    }
    return abs.lexically_normal();
}

bool pathsEqual(const fs::path& a, const fs::path& b)
{
    auto sa = normalizePath(a).generic_string();
    auto sb = normalizePath(b).generic_string();
#ifdef _WIN32
    std::transform(sa.begin(), sa.end(), sa.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    std::transform(sb.begin(), sb.end(), sb.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
#endif
    return sa == sb;
}

} // namespace

std::vector<std::string> splitCommandLine(const std::string& command)
{
    std::vector<std::string> args;
    std::string current;
    bool inSingle = false;
    bool inDouble = false;
    for (std::size_t i = 0; i < command.size(); ++i)
    {
        const char c = command[i];
        if (inSingle)
        {
            if (c == '\'')
            {
                inSingle = false;
            }
            else
            {
                current.push_back(c);
            }
            continue;
        }
        if (inDouble)
        {
            if (c == '\\' && i + 1 < command.size())
            {
                current.push_back(command[++i]);
                continue;
            }
            if (c == '"')
            {
                inDouble = false;
            }
            else
            {
                current.push_back(c);
            }
            continue;
        }
        if (c == '\'')
        {
            inSingle = true;
            continue;
        }
        if (c == '"')
        {
            inDouble = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!current.empty())
            {
                args.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty())
    {
        args.push_back(current);
    }
    return args;
}

std::vector<CompileCommand> loadCompileCommands(const fs::path& path,
                                                std::string& errorMessage)
{
    auto text = readTextFile(path, errorMessage);
    if (!text)
    {
        return {};
    }

    std::vector<CompileCommand> entries;
    for (auto object : splitObjects(*text))
    {
        auto file = extractJsonString(object, "file");
        if (!file)
        {
            continue;
        }
        CompileCommand entry;
        if (auto dir = extractJsonString(object, "directory"))
        {
            entry.directory = fs::path(*dir);
        }
        entry.file = fs::path(*file);
        if (!entry.directory.empty() && entry.file.is_relative())
        {
            entry.file = entry.directory / entry.file;
        }
        if (auto args = extractJsonStringArray(object, "arguments"))
        {
            entry.arguments = std::move(*args);
        }
        if (auto cmd = extractJsonString(object, "command"))
        {
            entry.command = std::move(*cmd);
        }
        entries.push_back(std::move(entry));
    }

    if (entries.empty())
    {
        errorMessage = "no compile commands found in " + path.string();
    }
    return entries;
}

std::optional<CompileCommand>
findCompileCommand(const std::vector<CompileCommand>& entries,
                   const fs::path& target)
{
    for (const auto& entry : entries)
    {
        if (pathsEqual(entry.file, target))
        {
            return entry;
        }
    }
    // Fallback: match by filename.
    const auto name = target.filename();
    for (const auto& entry : entries)
    {
        if (entry.file.filename() == name)
        {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<std::string>
syntaxOnlyArgsFromCompileCommand(const CompileCommand& entry,
                                 const fs::path& target)
{
    std::vector<std::string> args;
    if (!entry.arguments.empty())
    {
        args = entry.arguments;
    }
    else if (!entry.command.empty())
    {
        args = splitCommandLine(entry.command);
    }

    if (args.empty())
    {
        return {"-std=c++20", "-fsyntax-only", target.string()};
    }

    // Drop the compiler executable (first token).
    if (!args.empty())
    {
        args.erase(args.begin());
    }

    std::vector<std::string> filtered;
    filtered.reserve(args.size() + 2);
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a == "-c" || a == "-S" || a == "-E")
        {
            continue;
        }
        if (a == "-o" || a == "-MF" || a == "-MT" || a == "-MQ")
        {
            if (i + 1 < args.size())
            {
                ++i;
            }
            continue;
        }
        if (a.rfind("-o", 0) == 0 && a.size() > 2)
        {
            continue;
        }
        if (a == "-MMD" || a == "-MD" || a == "-MP")
        {
            continue;
        }
        filtered.push_back(a);
    }

    // Ensure -fsyntax-only is present.
    bool hasSyntaxOnly = false;
    for (const auto& a : filtered)
    {
        if (a == "-fsyntax-only")
        {
            hasSyntaxOnly = true;
            break;
        }
    }
    if (!hasSyntaxOnly)
    {
        filtered.insert(filtered.begin(), "-fsyntax-only");
    }

    // Ensure target file is present as an input.
    bool hasTarget = false;
    for (const auto& a : filtered)
    {
        if (a == target.string() || fs::path(a).filename() == target.filename())
        {
            hasTarget = true;
            break;
        }
    }
    if (!hasTarget)
    {
        filtered.push_back(target.string());
    }

    return filtered;
}

} // namespace cppsentinel
