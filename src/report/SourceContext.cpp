// Snippet enrichment: read each file once, attach the offending line.

#include "cppsentinel/report/SourceContext.h"

#include "cppsentinel/util/FileIo.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cppsentinel
{
namespace
{

constexpr std::size_t kMaxSnippetLength = 240;

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

std::string trimSnippet(std::string line)
{
    while (!line.empty() &&
           (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
    {
        line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    line.erase(0, start);
    if (line.size() > kMaxSnippetLength)
    {
        line.resize(kMaxSnippetLength);
        line += "...";
    }
    return line;
}

} // namespace

void attachSnippets(UnifiedReport& report,
                    const std::filesystem::path& projectRoot)
{
    std::map<std::string, std::vector<std::string>> fileCache;

    for (auto& finding : report.findings)
    {
        if (!finding.line || finding.file.empty())
        {
            continue;
        }

        const std::string key = finding.file.generic_string();
        auto it = fileCache.find(key);
        if (it == fileCache.end())
        {
            std::filesystem::path path = finding.file;
            if (path.is_relative() && !projectRoot.empty())
            {
                path = projectRoot / path;
            }
            std::string error;
            std::vector<std::string> lines;
            if (auto text = readTextFile(path, error))
            {
                lines = splitLines(*text);
            }
            it = fileCache.emplace(key, std::move(lines)).first;
        }

        const auto& lines = it->second;
        const std::size_t index = static_cast<std::size_t>(*finding.line);
        if (index == 0 || index > lines.size())
        {
            continue;
        }
        finding.snippet = trimSnippet(lines[index - 1]);
    }
}

} // namespace cppsentinel
