// Target expansion (see TargetEnumerator.h).

#include "cppsentinel/core/TargetEnumerator.h"

#include "cppsentinel/report/Filter.h"

#include <algorithm>
#include <system_error>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;

std::string lowerExtension(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return ext;
}

/// Path relative to projectRoot when possible, '/'-separated.
std::string relativeForMatch(const fs::path& path, const fs::path& projectRoot)
{
    if (projectRoot.empty())
    {
        return path.generic_string();
    }
    std::error_code ec;
    const auto relative = fs::relative(path, projectRoot, ec);
    if (ec || relative.empty())
    {
        return path.generic_string();
    }
    const std::string generic = relative.generic_string();
    if (generic.rfind("..", 0) == 0)
    {
        return path.generic_string();
    }
    return generic;
}

} // namespace

bool isCppSourceFile(const fs::path& path)
{
    static const char* kExtensions[] = {
        ".c",   ".cc",  ".cpp", ".cxx", ".c++", ".cp",  ".h",  ".hh",
        ".hpp", ".hxx", ".h++", ".ipp", ".inl", ".tcc", ".m",  ".mm",
    };
    const std::string ext = lowerExtension(path);
    if (ext.empty())
    {
        return false;
    }
    for (const char* candidate : kExtensions)
    {
        if (ext == candidate)
        {
            return true;
        }
    }
    return false;
}

TargetSet enumerateTargets(const fs::path& target,
                           const fs::path& projectRoot,
                           const std::vector<std::string>& excludeGlobs)
{
    TargetSet result;

    std::error_code ec;
    if (fs::is_regular_file(target, ec))
    {
        result.files.push_back(target);
        return result;
    }

    if (!fs::is_directory(target, ec))
    {
        return result;
    }

    auto excluded = [&](const fs::path& path) {
        if (excludeGlobs.empty())
        {
            return false;
        }
        const std::string relative = relativeForMatch(path, projectRoot);
        for (const auto& pattern : excludeGlobs)
        {
            if (globMatch(pattern, relative))
            {
                return true;
            }
        }
        return false;
    };

    // Directory walk: skip unreadable subtrees instead of failing the run.
    fs::recursive_directory_iterator it(
        target, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        result.truncated = true;
        return result;
    }

    const fs::recursive_directory_iterator end;
    while (it != end)
    {
        const fs::path path = it->path();

        if (it->is_directory(ec))
        {
            const std::string name = path.filename().string();
            if (!name.empty() && (name.front() == '.' || name == "build" ||
                                  name.rfind("build-", 0) == 0 ||
                                  name == "node_modules"))
            {
                it.disable_recursion_pending();
            }
            it.increment(ec);
            if (ec)
            {
                result.truncated = true;
                break;
            }
            continue;
        }

        if (!it->is_regular_file(ec) || !isCppSourceFile(path))
        {
            it.increment(ec);
            if (ec)
            {
                result.truncated = true;
                break;
            }
            continue;
        }

        if (excluded(path))
        {
            ++result.excluded;
        }
        else
        {
            result.files.push_back(path);
        }

        it.increment(ec);
        if (ec)
        {
            result.truncated = true;
            break;
        }
    }

    std::sort(result.files.begin(), result.files.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.generic_string() < b.generic_string();
              });
    result.files.erase(std::unique(result.files.begin(), result.files.end()),
                       result.files.end());
    return result;
}

} // namespace cppsentinel
