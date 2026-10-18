#pragma once

// Baseline (accepted known findings) recording and comparison.
//
// A baseline stores stable finding ids plus their rule/path/message so that a
// moved finding (same file + rule + message, different line) is still
// recognised as known rather than reported as new.

#include "cppsentinel/report/Findings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cppsentinel
{

struct BaselineEntry
{
    std::string id;
    std::string rule;
    std::string file;
    long long line = 0;
    std::string message;
};

struct BaselineData
{
    std::uint32_t version = 1;
    std::string generated_at;
    std::vector<BaselineEntry> findings;
};

/// Build a baseline document from the current (already filtered) findings.
BaselineData baselineFromReport(const UnifiedReport& report);

std::string serializeBaseline(const BaselineData& baseline);

bool loadBaseline(const std::filesystem::path& path, BaselineData& baseline,
                  std::string& errorMessage);

bool saveBaseline(const std::filesystem::path& path, const BaselineData& baseline,
                  std::string& errorMessage);

/// Mark every finding as new/known and record vanished entries as fixed.
void applyBaseline(UnifiedReport& report, const BaselineData& baseline,
                   const std::filesystem::path& baselinePath);

} // namespace cppsentinel
