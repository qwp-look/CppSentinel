#pragma once

// Target expansion: turn a file-or-directory target into the explicit file list
// every checker analyses. Without this, clang-tidy (driven by the compilation
// database) and cppcheck (driven by directory walking) can quietly disagree
// about which files they looked at.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace cppsentinel
{

struct TargetSet
{
    std::vector<std::filesystem::path> files; ///< absolute, sorted, unique
    std::size_t excluded = 0;                 ///< files dropped by exclude globs
    bool truncated = false;                   ///< directory walk hit an error
};

/// True for extensions handled by the built-in checkers.
bool isCppSourceFile(const std::filesystem::path& path);

/// Expand `target`. A regular file yields itself (even if it has an unknown
/// extension: the user asked for it explicitly). A directory is walked
/// recursively; `excludeGlobs` match project-relative '/'-separated paths.
TargetSet enumerateTargets(const std::filesystem::path& target,
                           const std::filesystem::path& projectRoot,
                           const std::vector<std::string>& excludeGlobs = {});

} // namespace cppsentinel
