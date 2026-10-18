#pragma once

// Coarse per-checker result cache.
//
// Key = checker kind + options + extra_args + tool version + a fingerprint of
// every file under the project root (path/size/mtime). Any project edit, tool
// upgrade or option change invalidates it; nothing finer is attempted because a
// header edit is invisible to a per-target-file hash.
//
// Opt-in via [run].cache = true. A cached result carries diagnostics only - no
// logs or artifacts - so reports stay honest about what actually ran.

#include "cppsentinel/config/AppConfig.h"
#include "cppsentinel/report/Types.h"

#include <filesystem>
#include <optional>
#include <string>

namespace cppsentinel
{

/// FNV-1a hex digest of `text`, used for cache file names.
std::string hashKey(std::string_view text);

/// Fingerprint of all C/C++-relevant files under `root` (skips build/, .git/,
/// report/ and dot-directories). Returns an empty string when nothing is found.
std::string projectFingerprint(const std::filesystem::path& root);

class ResultCache
{
  public:
    ResultCache(std::filesystem::path directory, bool enabled);

    bool enabled() const noexcept
    {
        return enabled_;
    }

    /// Build the lookup key for one checker invocation.
    std::string keyFor(const CheckerConfig& checker, const std::string& version,
                       const std::string& fingerprint) const;

    std::optional<CheckerResult> load(const std::string& key) const;

    void store(const std::string& key, const CheckerResult& result) const;

  private:
    std::filesystem::path directory_;
    bool enabled_ = false;
};

} // namespace cppsentinel
