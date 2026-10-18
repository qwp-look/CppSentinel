#pragma once

// Path and token helpers shared by config, report, and MSYS2 runner.

#include <filesystem>
#include <string>
#include <string_view>

namespace cppsentinel
{

/// ASCII case-insensitive equality (environment names, env keys).
bool iequals(std::string_view a, std::string_view b);

/// Convert a local path to an MSYS2 POSIX path (C:\a\b -> /c/a/b).
std::string toMsysPath(const std::filesystem::path& input);

/// Single-quote a value for bash (it's -> 'it'\''s').
std::string shellSingleQuote(std::string_view value);

/// Keep only characters safe for report log/artifact file names.
std::string sanitizeFileToken(std::string_view name);

/// Resolve a bare command name through PATH, returning the full path when
/// found. Accepts Windows (';') and MSYS (':') PATH syntax and '/c/...'
/// entries, so a binary started from an MSYS shell can still launch native
/// tools.
std::filesystem::path resolveExecutablePath(std::string_view name);

/// Resolve path relative to baseDir when not absolute.
std::filesystem::path
resolveAgainst(const std::filesystem::path& baseDir,
               const std::filesystem::path& maybeRelative);

} // namespace cppsentinel
