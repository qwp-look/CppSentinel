#pragma once

// Exclusive report/<run-id>/ creation and report publishing.
//
// Artifacts per run:
//   report.json      unified findings (schema 3) - machine source of truth
//   report.raw.json  raw per-checker diagnostics (schema 2) - traceability
//   report.sarif     SARIF 2.1.0 for code-scanning consumers
//   report.md        human-readable report
//   summary.txt      plain-text console summary

#include "cppsentinel/report/Findings.h"
#include "cppsentinel/report/Types.h"

#include <filesystem>
#include <optional>
#include <string>

namespace cppsentinel
{

struct ReportLayout
{
    std::filesystem::path root;          ///< report/<run-id>/
    std::filesystem::path logs_dir;      ///< .../logs
    std::filesystem::path artifacts_dir; ///< .../artifacts
    std::string run_id;
};

/// Create a unique report tree. Never reuses an existing directory (no create_directories race reuse).
std::optional<ReportLayout>
createReportLayout(const std::filesystem::path& baseDir, std::string& errorMessage);

class ReportWriter
{
public:
    explicit ReportWriter(ReportLayout layout);

    const ReportLayout& layout() const noexcept
    {
        return layout_;
    }

    std::filesystem::path checkerStdoutLog(const std::string& checkerId) const;
    std::filesystem::path checkerStderrLog(const std::string& checkerId) const;
    std::filesystem::path checkerArtifactDir(const std::string& checkerId) const;

    /// Publish all report artifacts. Builds the unified layer from `report`
    /// using its project root as the normalization base.
    bool writeReport(const RunReport& report, std::string& errorMessage) const;

    /// Publish all report artifacts using a pre-built unified layer.
    bool writeReport(const RunReport& report, const UnifiedReport& unified,
                     std::string& errorMessage) const;

private:
    bool writeArtifacts(const RunReport& report, const UnifiedReport& unified,
                        std::string& errorMessage) const;

    ReportLayout layout_;
};

} // namespace cppsentinel
