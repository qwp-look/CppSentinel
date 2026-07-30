// Exclusive report directories + atomic report.json publish.

#include "cppsentinel/report/ReportWriter.h"

#include "cppsentinel/process/PathUtil.h"
#include "cppsentinel/report/ConsoleReporter.h"
#include "cppsentinel/report/JsonReporter.h"
#include "cppsentinel/util/FileIo.h"

#include <fstream>
#include <random>

namespace cppsentinel
{
namespace
{

namespace fs = std::filesystem;

bool tryCreateExclusiveDir(const fs::path& path, std::string& errorMessage)
{
    // create_directories reuses existing trees; use create_directory on the leaf
    // after ensuring the parent exists, and require that the leaf did not exist.
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty())
    {
        fs::create_directories(parent, ec);
        if (ec)
        {
            errorMessage =
                "failed to create report parent '" + parent.string() + "': " +
                ec.message();
            return false;
        }
    }

    if (fs::exists(path, ec))
    {
        errorMessage = "report directory already exists: " + path.string();
        return false;
    }

    if (!fs::create_directory(path, ec) || ec)
    {
        errorMessage = "failed to create exclusive report directory '" +
                       path.string() + "': " +
                       (ec ? ec.message() : "unknown error");
        return false;
    }
    return true;
}

bool directoryIsWritable(const fs::path& dir, std::string& errorMessage)
{
    const fs::path probe = dir / ".cpp_sentinel_write_test";
    {
        std::ofstream out(probe, std::ios::out | std::ios::trunc);
        if (!out)
        {
            errorMessage = "report directory is not writable: " + dir.string();
            return false;
        }
        out << "ok\n";
    }
    std::error_code removeEc;
    fs::remove(probe, removeEc);
    return true;
}

} // namespace

std::optional<ReportLayout> createReportLayout(const fs::path& baseDir,
                                               std::string& errorMessage)
{
    std::error_code ec;
    const fs::path absoluteBase = fs::absolute(baseDir, ec);
    if (ec)
    {
        errorMessage = "cannot resolve report base '" + baseDir.string() + "': " +
                       ec.message();
        return std::nullopt;
    }

    const std::string stamp = makeRunIdTimestamp();
    if (stamp.empty())
    {
        errorMessage = "failed to format run id timestamp";
        return std::nullopt;
    }

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 9999);

    for (int attempt = 0; attempt < 64; ++attempt)
    {
        std::string runId = stamp;
        if (attempt > 0)
        {
            runId += "_" + std::to_string(attempt) + "_" +
                     std::to_string(dist(rng));
        }

        const fs::path root = absoluteBase / runId;
        std::string createError;
        if (!tryCreateExclusiveDir(root, createError))
        {
            // Collision / race: retry with a new suffix.
            if (createError.find("already exists") != std::string::npos)
            {
                continue;
            }
            errorMessage = createError;
            return std::nullopt;
        }

        if (!directoryIsWritable(root, errorMessage))
        {
            return std::nullopt;
        }

        ReportLayout layout;
        layout.root = root;
        layout.run_id = runId;
        layout.logs_dir = root / "logs";
        layout.artifacts_dir = root / "artifacts";

        fs::create_directories(layout.logs_dir, ec);
        if (ec)
        {
            errorMessage = "failed to create logs dir: " + ec.message();
            return std::nullopt;
        }
        fs::create_directories(layout.artifacts_dir, ec);
        if (ec)
        {
            errorMessage = "failed to create artifacts dir: " + ec.message();
            return std::nullopt;
        }
        return layout;
    }

    errorMessage = "exhausted retries creating unique report directory under " +
                   absoluteBase.string();
    return std::nullopt;
}

ReportWriter::ReportWriter(ReportLayout layout) : layout_(std::move(layout)) {}

fs::path ReportWriter::checkerStdoutLog(const std::string& checkerId) const
{
    return layout_.logs_dir / (sanitizeFileToken(checkerId) + ".stdout.log");
}

fs::path ReportWriter::checkerStderrLog(const std::string& checkerId) const
{
    return layout_.logs_dir / (sanitizeFileToken(checkerId) + ".stderr.log");
}

fs::path ReportWriter::checkerArtifactDir(const std::string& checkerId) const
{
    return layout_.artifacts_dir / sanitizeFileToken(checkerId);
}

bool ReportWriter::writeReport(const RunReport& report,
                               std::string& errorMessage) const
{
    const auto jsonPath = layout_.root / "report.json";
    const auto summaryPath = layout_.root / "summary.txt";

    // Write JSON to a temp file then rename for a more atomic publish.
    const auto tmpJson = layout_.root / "report.json.tmp";
    const std::string json = serializeRunReportJson(report);
    if (!writeTextFile(tmpJson, json, errorMessage))
    {
        return false;
    }

    std::error_code ec;
    fs::rename(tmpJson, jsonPath, ec);
    if (ec)
    {
        // Fallback copy+remove if rename across odd FS fails.
        ec.clear();
        fs::copy_file(tmpJson, jsonPath, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            errorMessage = "failed to publish report.json: " + ec.message();
            return false;
        }
        fs::remove(tmpJson, ec);
    }

    const std::string summary = formatSummaryText(report);
    if (!writeTextFile(summaryPath, summary, errorMessage))
    {
        return false;
    }
    return true;
}

} // namespace cppsentinel
