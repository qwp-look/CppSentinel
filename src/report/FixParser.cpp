// clang-tidy fixes YAML subset parser (see FixParser.h).

#include "cppsentinel/report/FixParser.h"

#include "cppsentinel/report/Findings.h"
#include "cppsentinel/util/FileIo.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cppsentinel
{
namespace
{

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

std::string collapseWhitespace(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (char c : text)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace)
        {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(c);
    }
    return out;
}

/// YAML scalar: plain, single-quoted ('' escapes a quote) or double-quoted.
/// Returns false for block scalars (>, |) which this parser does not interpret.
bool parseScalar(std::string_view raw, std::string& out)
{
    raw = trim(raw);
    out.clear();
    if (raw.empty())
    {
        return true;
    }

    if (raw.front() == '\'' || raw.front() == '"')
    {
        const char quote = raw.front();
        std::size_t i = 1;
        while (i < raw.size())
        {
            if (raw[i] == quote)
            {
                if (i + 1 < raw.size() && raw[i + 1] == quote)
                {
                    out.push_back(quote);
                    i += 2;
                    continue;
                }
                return true;
            }
            if (quote == '"' && raw[i] == '\\' && i + 1 < raw.size())
            {
                const char escaped = raw[i + 1];
                switch (escaped)
                {
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                default:
                    out.push_back(escaped);
                    break;
                }
                i += 2;
                continue;
            }
            out.push_back(raw[i]);
            ++i;
        }
        return true; // unterminated: take what we have
    }

    if (raw.front() == '>' || raw.front() == '|')
    {
        return false; // block scalar
    }

    out.assign(raw);
    return true;
}

std::string keyOf(std::string_view line, std::string_view& value)
{
    const auto colon = line.find(':');
    if (colon == std::string_view::npos)
    {
        value = {};
        return std::string(trim(line));
    }
    value = line.substr(colon + 1);
    return std::string(trim(line.substr(0, colon)));
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        auto end = text.find('\n', pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (end == text.size())
        {
            break;
        }
        pos = end + 1;
    }
    return lines;
}

void flushReplacement(ParsedFix& fix, FixReplacement& replacement, bool& active)
{
    if (!active)
    {
        return;
    }
    fix.replacements.push_back(replacement);
    replacement = FixReplacement{};
    active = false;
}

} // namespace

std::vector<ParsedFix> parseClangTidyFixes(const std::string& yamlText)
{
    std::vector<ParsedFix> fixes;

    ParsedFix current;
    bool hasCurrent = false;
    bool inReplacements = false;
    bool replacementActive = false;
    FixReplacement replacement;

    auto flushCurrent = [&]() {
        flushReplacement(current, replacement, replacementActive);
        if (hasCurrent && !current.rule.empty())
        {
            fixes.push_back(std::move(current));
        }
        current = ParsedFix{};
        hasCurrent = false;
        inReplacements = false;
    };

    for (const auto& line : splitLines(yamlText))
    {
        std::string_view trimmed = trim(line);
        if (trimmed.empty() || trimmed == "---" || trimmed.front() == '#')
        {
            continue;
        }

        // "- DiagnosticName: x" starts a diagnostic
        if (trimmed.size() > 2 && trimmed[0] == '-' &&
            trimmed.compare(1, 1, " ") == 0)
        {
            std::string_view body = trim(trimmed.substr(1));
            if (body.rfind("DiagnosticName:", 0) == 0)
            {
                flushCurrent();
                hasCurrent = true;
                std::string_view value;
                keyOf(body, value);
                parseScalar(value, current.rule);
                continue;
            }
            if (body.rfind("FilePath:", 0) == 0 && inReplacements)
            {
                flushReplacement(current, replacement, replacementActive);
                std::string_view value;
                keyOf(body, value);
                std::string path;
                if (parseScalar(value, path))
                {
                    replacement.file = path;
                    replacementActive = true;
                }
                continue;
            }
            continue;
        }

        if (!hasCurrent)
        {
            continue;
        }

        std::string_view value;
        const std::string key = keyOf(trimmed, value);

        if (key == "DiagnosticName")
        {
            parseScalar(value, current.rule);
        }
        else if (key == "Message")
        {
            std::string message;
            if (parseScalar(value, message))
            {
                current.message = collapseWhitespace(message);
            }
            else
            {
                current.message.clear();
            }
        }
        else if (key == "Replacements")
        {
            inReplacements = true;
        }
        else if (inReplacements && key == "Offset")
        {
            replacement.offset = std::strtoll(std::string(trim(value)).c_str(),
                                              nullptr, 10);
        }
        else if (inReplacements && key == "Length")
        {
            replacement.length = std::strtoll(std::string(trim(value)).c_str(),
                                              nullptr, 10);
        }
        else if (inReplacements && key == "ReplacementText")
        {
            std::string text;
            if (parseScalar(value, text))
            {
                replacement.text = std::move(text);
            }
        }
        else if (inReplacements && key == "FilePath")
        {
            std::string path;
            if (parseScalar(value, path))
            {
                replacement.file = path;
                replacementActive = true;
            }
        }
    }

    flushCurrent();
    return fixes;
}

void attachFixes(UnifiedReport& report, const std::string& yamlText,
                 const std::filesystem::path& projectRoot)
{
    const auto parsed = parseClangTidyFixes(yamlText);
    if (parsed.empty() || report.findings.empty())
    {
        return;
    }

    // Resolve the line for each parsed fix from its first replacement offset.
    std::map<std::string, std::vector<std::string>> lineCache;
    auto lineForOffset = [&](const std::filesystem::path& file,
                             long long offset) -> std::uint32_t {
        const std::string key = file.generic_string();
        auto it = lineCache.find(key);
        if (it == lineCache.end())
        {
            std::string error;
            std::string text;
            if (auto content = readTextFile(file, error))
            {
                text = *content;
            }
            std::vector<std::string> lines;
            std::size_t start = 0;
            while (start <= text.size())
            {
                auto end = text.find('\n', start);
                if (end == std::string::npos)
                {
                    end = text.size();
                }
                lines.push_back(text.substr(start, end - start));
                if (end == text.size())
                {
                    break;
                }
                start = end + 1;
            }
            it = lineCache.emplace(key, std::move(lines)).first;
        }

        const auto& lines = it->second;
        std::size_t remaining = offset < 0 ? 0 : static_cast<std::size_t>(offset);
        std::uint32_t line = 1;
        for (const auto& text : lines)
        {
            const std::size_t span = text.size() + 1; // include newline
            if (remaining < span)
            {
                return line;
            }
            remaining -= span;
            ++line;
        }
        return line;
    };

    for (const auto& fix : parsed)
    {
        if (fix.replacements.empty())
        {
            continue;
        }

        std::uint32_t line = 0;
        std::filesystem::path relativeFile;
        const auto& first = fix.replacements.front();
        if (!first.file.empty())
        {
            relativeFile = normalizeFindingPath(first.file, projectRoot);
            if (!relativeFile.is_relative())
            {
                relativeFile = first.file; // outside root: keep absolute
            }
            line = lineForOffset(first.file, first.offset);
        }

        Finding* target = nullptr;
        if (!relativeFile.empty() && line > 0)
        {
            for (auto& finding : report.findings)
            {
                if (finding.line && *finding.line == line &&
                    finding.file.generic_string() == relativeFile.generic_string())
                {
                    target = &finding;
                    break;
                }
            }
        }

        if (!target && !fix.message.empty())
        {
            // Position inconclusive: only trust an unambiguous rule+message match.
            Finding* candidate = nullptr;
            int matches = 0;
            for (auto& finding : report.findings)
            {
                if (finding.rule == fix.rule && finding.message == fix.message)
                {
                    candidate = &finding;
                    ++matches;
                }
            }
            if (matches == 1)
            {
                target = candidate;
            }
        }

        if (!target)
        {
            continue;
        }

        Fix attached;
        attached.description = fix.message;
        for (auto& replacement : fix.replacements)
        {
            FixReplacement copy = replacement;
            if (!copy.file.empty())
            {
                copy.file = normalizeFindingPath(copy.file, projectRoot);
            }
            attached.replacements.push_back(std::move(copy));
        }
        target->fixes.push_back(std::move(attached));
    }
}

} // namespace cppsentinel
