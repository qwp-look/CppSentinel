// Hand-written JSON serializer for RunReport (no extra JSON dependency).

#include "cppsentinel/report/JsonReporter.h"

#include <cstdio>
#include <sstream>
#include <string_view>

namespace cppsentinel
{
namespace
{

std::string jsonEscape(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            }
            else
            {
                out.push_back(c);
            }
            break;
        }
    }
    return out;
}

void writeString(std::ostringstream& oss, std::string_view key, std::string_view value,
                 bool comma)
{
    if (comma)
    {
        oss << ',';
    }
    oss << '"' << jsonEscape(key) << "\":\"" << jsonEscape(value) << '"';
}

void writePath(std::ostringstream& oss, std::string_view key,
               const std::filesystem::path& path, bool comma)
{
    writeString(oss, key, path.generic_string(), comma);
}

void writeNumber(std::ostringstream& oss, std::string_view key, long long value,
                 bool comma)
{
    if (comma)
    {
        oss << ',';
    }
    oss << '"' << jsonEscape(key) << "\":" << value;
}

void writeBool(std::ostringstream& oss, std::string_view key, bool value, bool comma)
{
    if (comma)
    {
        oss << ',';
    }
    oss << '"' << jsonEscape(key) << "\":" << (value ? "true" : "false");
}

} // namespace

std::string serializeRunReportJson(const RunReport& report)
{
    std::ostringstream oss;
    oss << '{';
    writeNumber(oss, "schema_version", report.schema_version, false);
    writeNumber(oss, "exit_code", report.exit_code, true);

    oss << ",\"metadata\":{";
    writeString(oss, "run_id", report.metadata.run_id, false);
    writePath(oss, "config_path", report.metadata.config_path, true);
    writePath(oss, "target_path", report.metadata.target_path, true);
    writePath(oss, "project_root", report.metadata.project_root, true);
    writePath(oss, "report_dir", report.metadata.report_dir, true);
    writeString(oss, "started_at", report.metadata.started_at, true);
    writeString(oss, "finished_at", report.metadata.finished_at, true);
    writeNumber(oss, "duration_ms", report.metadata.duration.count(), true);
    oss << '}';

    oss << ",\"checkers\":[";
    for (std::size_t i = 0; i < report.checkers.size(); ++i)
    {
        const auto& c = report.checkers[i];
        if (i > 0)
        {
            oss << ',';
        }
        oss << '{';
        writeString(oss, "id", c.checker_id, false);
        writeString(oss, "kind", c.checker_kind, true);
        writeString(oss, "status", toString(c.status), true);
        if (c.exit_code)
        {
            writeNumber(oss, "exit_code", *c.exit_code, true);
        }
        else
        {
            oss << ",\"exit_code\":null";
        }
        writeNumber(oss, "duration_ms", c.duration.count(), true);
        writeString(oss, "message", c.message, true);
        writePath(oss, "stdout_log", c.stdout_log, true);
        writePath(oss, "stderr_log", c.stderr_log, true);

        oss << ",\"artifacts\":[";
        for (std::size_t a = 0; a < c.artifacts.size(); ++a)
        {
            if (a > 0)
            {
                oss << ',';
            }
            oss << '"' << jsonEscape(c.artifacts[a].generic_string()) << '"';
        }
        oss << ']';

        oss << ",\"diagnostics\":[";
        for (std::size_t d = 0; d < c.diagnostics.size(); ++d)
        {
            const auto& diag = c.diagnostics[d];
            if (d > 0)
            {
                oss << ',';
            }
            oss << '{';
            writePath(oss, "file", diag.file, false);
            if (diag.line)
            {
                writeNumber(oss, "line", *diag.line, true);
            }
            else
            {
                oss << ",\"line\":null";
            }
            if (diag.column)
            {
                writeNumber(oss, "column", *diag.column, true);
            }
            else
            {
                oss << ",\"column\":null";
            }
            writeString(oss, "severity", toString(diag.severity), true);
            writeString(oss, "message", diag.message, true);
            writeString(oss, "rule", diag.rule, true);
            oss << '}';
        }
        oss << ']';
        oss << '}';
    }
    oss << ']';
    oss << '}';
    return oss.str();
}

} // namespace cppsentinel
