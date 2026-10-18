// Config ignore lists + in-source suppression comments.

#include "cppsentinel/report/Filter.h"

#include "cppsentinel/util/FileIo.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cppsentinel
{
namespace
{

char lowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool containsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
    {
        return false;
    }
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) { return lowerAscii(a) == lowerAscii(b); });
    return it != haystack.end();
}

bool globMatchImpl(std::string_view pattern, std::string_view text)
{
    while (!pattern.empty())
    {
        if (pattern.front() == '*')
        {
            const bool crossSegment = pattern.size() > 1 && pattern[1] == '*';
            std::string_view rest = pattern.substr(crossSegment ? 2 : 1);

            if (crossSegment && !rest.empty() && rest.front() == '/')
            {
                // "**/" may match zero directories.
                if (globMatchImpl(rest.substr(1), text))
                {
                    return true;
                }
            }

            for (std::size_t i = 0; i <= text.size(); ++i)
            {
                if (!crossSegment && i > 0 && text[i - 1] == '/')
                {
                    break; // single '*' never crosses a separator
                }
                if (globMatchImpl(rest, text.substr(i)))
                {
                    return true;
                }
            }
            return false;
        }

        if (text.empty())
        {
            return false;
        }
        if (pattern.front() == '?')
        {
            if (text.front() == '/')
            {
                return false;
            }
        }
        else if (lowerAscii(pattern.front()) != lowerAscii(text.front()))
        {
            return false;
        }
        pattern.remove_prefix(1);
        text.remove_prefix(1);
    }
    return text.empty();
}

struct Directive
{
    std::uint32_t line = 0;   ///< line the comment sits on
    bool restOfFile = true;   ///< "disable" vs "disable-next-line"
    std::vector<std::string> rules; ///< empty = all rules
};

using DirectiveList = std::vector<Directive>;

constexpr std::string_view kMarker = "cppsentinel-disable";
constexpr std::string_view kNextLineSuffix = "-next-line";

std::vector<std::string> splitRules(std::string_view text)
{
    std::vector<std::string> rules;
    std::string current;
    for (char c : text)
    {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c)))
        {
            if (!current.empty())
            {
                rules.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty())
    {
        rules.push_back(current);
    }
    return rules;
}

DirectiveList parseDirectives(std::string_view source)
{
    DirectiveList directives;
    std::uint32_t lineNo = 0;
    std::size_t pos = 0;
    while (pos <= source.size())
    {
        auto end = source.find('\n', pos);
        if (end == std::string_view::npos)
        {
            end = source.size();
        }
        auto line = source.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;

        const auto marker = line.find(kMarker);
        if (marker == std::string_view::npos)
        {
            continue;
        }

        Directive directive;
        directive.line = lineNo;

        std::string_view rest = line.substr(marker + kMarker.size());
        if (rest.substr(0, kNextLineSuffix.size()) == kNextLineSuffix)
        {
            directive.restOfFile = false;
            rest.remove_prefix(kNextLineSuffix.size());
        }

        // Drop trailing comment terminators before tokenizing.
        if (const auto cut = rest.find("*/"); cut != std::string_view::npos)
        {
            rest = rest.substr(0, cut);
        }
        directive.rules = splitRules(rest);

        // A bare "//" token comes from a stray comment marker.
        directive.rules.erase(
            std::remove_if(directive.rules.begin(), directive.rules.end(),
                           [](const std::string& rule) {
                               return rule == "//" || rule == "/*" || rule.empty();
                           }),
            directive.rules.end());

        directives.push_back(std::move(directive));
    }
    return directives;
}

bool directivesSuppress(const DirectiveList& directives, std::uint32_t line,
                        const std::string& rule)
{
    for (const auto& directive : directives)
    {
        const bool inScope = directive.restOfFile ? line >= directive.line
                                                 : line == directive.line + 1;
        if (!inScope)
        {
            continue;
        }
        if (directive.rules.empty())
        {
            return true;
        }
        for (const auto& pattern : directive.rules)
        {
            if (globMatch(pattern, rule))
            {
                return true;
            }
        }
    }
    return false;
}

std::string configReason(const FilterSettings& settings, const Finding& finding)
{
    if (settings.ignore_outside_project && finding.file.is_absolute())
    {
        // System/vendored headers outside the project: not the user's code.
        return "config:outside_project";
    }
    if (settings.has_min_severity &&
        severityRank(finding.severity) > severityRank(settings.min_severity))
    {
        return "config:min_severity";
    }
    for (const auto& pattern : settings.ignore_rules)
    {
        if (globMatch(pattern, finding.rule))
        {
            return "config:ignore_rules";
        }
    }
    const std::string path = finding.file.generic_string();
    for (const auto& pattern : settings.ignore_paths)
    {
        if (globMatch(pattern, path))
        {
            return "config:ignore_paths";
        }
    }
    for (const auto& needle : settings.ignore_messages)
    {
        if (containsInsensitive(finding.message, needle))
        {
            return "config:ignore_messages";
        }
    }
    return {};
}

} // namespace

bool globMatch(std::string_view pattern, std::string_view text)
{
    std::string normalized = std::string(text);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return globMatchImpl(pattern, normalized);
}

std::size_t applyFilters(UnifiedReport& report, const FilterSettings& settings,
                         const std::filesystem::path& projectRoot)
{
    const bool anyConfigFilter =
        settings.has_min_severity || !settings.ignore_rules.empty() ||
        !settings.ignore_paths.empty() || !settings.ignore_messages.empty() ||
        settings.ignore_outside_project;
    if (!anyConfigFilter && !settings.inline_suppressions)
    {
        return 0;
    }

    std::map<std::string, DirectiveList> directiveCache;
    std::vector<Finding> kept;
    kept.reserve(report.findings.size());

    for (const auto& finding : report.findings)
    {
        std::string reason = configReason(settings, finding);

        if (reason.empty() && settings.inline_suppressions && finding.line &&
            !finding.file.empty())
        {
            const std::string key = finding.file.generic_string();
            auto it = directiveCache.find(key);
            if (it == directiveCache.end())
            {
                std::filesystem::path path = finding.file;
                if (path.is_relative() && !projectRoot.empty())
                {
                    path = projectRoot / path;
                }
                std::string readError;
                DirectiveList directives;
                if (auto text = readTextFile(path, readError))
                {
                    directives = parseDirectives(*text);
                }
                it = directiveCache.emplace(key, std::move(directives)).first;
            }
            if (directivesSuppress(it->second, *finding.line, finding.rule))
            {
                reason = "inline:disable";
            }
        }

        if (reason.empty())
        {
            kept.push_back(finding);
            continue;
        }

        SuppressedFinding suppressed;
        suppressed.id = finding.id;
        suppressed.file = finding.file;
        suppressed.line = finding.line;
        suppressed.rule = finding.rule;
        suppressed.reason = std::move(reason);
        report.suppressed.push_back(std::move(suppressed));
    }

    if (report.suppressed.empty())
    {
        return 0;
    }

    report.findings = std::move(kept);
    recomputeSummary(report);
    return report.suppressed.size();
}

} // namespace cppsentinel
