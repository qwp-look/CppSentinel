// Parse clang/gcc/cppcheck-style "file:line:col: severity: message" lines.

#include "cppsentinel/checker/DiagnosticParser.h"

#include "cppsentinel/process/PathUtil.h"

#include <cctype>
#include <optional>
#include <string>

namespace cppsentinel
{
namespace
{

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<std::uint32_t> parseUint(std::string_view s)
{
    if (s.empty())
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (char c : s)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return std::nullopt;
        }
        value = value * 10u + static_cast<std::uint32_t>(c - '0');
    }
    return value;
}

// Split "path:line:col: rest" carefully for Windows drive letters (C:).
bool splitLocation(std::string_view line, std::string_view& file,
                   std::optional<std::uint32_t>& lineNo,
                   std::optional<std::uint32_t>& colNo, std::string_view& rest)
{
    std::size_t start = 0;
    if (line.size() >= 2 && std::isalpha(static_cast<unsigned char>(line[0])) &&
        line[1] == ':')
    {
        start = 2; // skip drive
    }

    const auto first = line.find(':', start);
    if (first == std::string_view::npos)
    {
        return false;
    }
    const auto second = line.find(':', first + 1);
    if (second == std::string_view::npos)
    {
        return false;
    }
    const auto third = line.find(':', second + 1);
    if (third == std::string_view::npos)
    {
        return false;
    }

    file = line.substr(0, first);
    lineNo = parseUint(line.substr(first + 1, second - first - 1));
    colNo = parseUint(line.substr(second + 1, third - second - 1));
    if (!lineNo)
    {
        return false;
    }
    rest = line.substr(third + 1);
    return true;
}

std::string extractRule(std::string& message)
{
    // trailing [rule-name]
    if (message.size() < 3 || message.back() != ']')
    {
        return {};
    }
    const auto open = message.rfind('[');
    if (open == std::string::npos || open + 1 >= message.size())
    {
        return {};
    }
    std::string rule = message.substr(open + 1, message.size() - open - 2);
    message.erase(open);
    while (!message.empty() &&
           std::isspace(static_cast<unsigned char>(message.back())))
    {
        message.pop_back();
    }
    return rule;
}

} // namespace

Severity parseSeverityToken(std::string_view token)
{
    token = trim(token);
    if (iequals(token, "error") || iequals(token, "fatal error") ||
        iequals(token, "fatal"))
    {
        return Severity::Error;
    }
    if (iequals(token, "warning") || iequals(token, "warn"))
    {
        return Severity::Warning;
    }
    if (iequals(token, "style") || iequals(token, "note") ||
        iequals(token, "remark"))
    {
        // clang "note" is informational; style tools may emit style.
        if (iequals(token, "style"))
        {
            return Severity::Style;
        }
        return Severity::Information;
    }
    if (iequals(token, "information") || iequals(token, "info") ||
        iequals(token, "performance") || iequals(token, "portability"))
    {
        return Severity::Information;
    }
    return Severity::Unknown;
}

std::vector<Diagnostic> parseClangStyleDiagnostics(std::string_view text)
{
    std::vector<Diagnostic> out;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        auto line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        pos = end + (end < text.size() ? 1 : 0);

        std::string_view file;
        std::optional<std::uint32_t> lineNo;
        std::optional<std::uint32_t> colNo;
        std::string_view rest;
        if (!splitLocation(line, file, lineNo, colNo, rest))
        {
            continue;
        }

        rest = trim(rest);
        const auto sevEnd = rest.find(':');
        if (sevEnd == std::string_view::npos)
        {
            continue;
        }
        const auto sevTok = trim(rest.substr(0, sevEnd));
        auto messageView = trim(rest.substr(sevEnd + 1));
        if (sevTok.empty() || messageView.empty())
        {
            continue;
        }

        Diagnostic d;
        d.file = std::filesystem::path(std::string(file));
        d.line = lineNo;
        d.column = colNo;
        d.severity = parseSeverityToken(sevTok);
        // "note:" lines annotate the preceding diagnostic (fix-it hints, why
        // the analyzer fired, ...). They are context, not independent findings.
        if (iequals(sevTok, "note") || iequals(sevTok, "remark"))
        {
            d.role = DiagnosticRole::Note;
        }
        std::string message(messageView);
        d.rule = extractRule(message);
        d.message = std::move(message);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<Diagnostic> parseCppcheckDiagnostics(std::string_view text)
{
    // Same shape as clang-style with our configured template.
    return parseClangStyleDiagnostics(text);
}

} // namespace cppsentinel
