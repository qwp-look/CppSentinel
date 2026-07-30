#pragma once

// Exclusive report/<run-id>/ creation and report.json / summary.txt publishing.

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

    /// Write report.json (temp + rename) and summary.txt under layout.root.
    bool writeReport(const RunReport& report, std::string& errorMessage) const;

private:
    ReportLayout layout_;
};

} // namespace cppsentinel
