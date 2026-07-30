#pragma once

// Small filesystem helpers used by config tests, reports, and checkers.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cppsentinel
{

std::optional<std::string> readTextFile(const std::filesystem::path& path,
                                        std::string& errorMessage);

bool writeTextFile(const std::filesystem::path& path, std::string_view content,
                   std::string& errorMessage);

/// Local wall-clock timestamp for metadata (ISO-like).
std::string nowIsoLocal();

/// run-id stamp: YYYY_MM_DD_HH_MM_SS_mmm
std::string makeRunIdTimestamp();

} // namespace cppsentinel
