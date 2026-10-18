#pragma once

// `CppSentinel doctor`: verify that a configuration can actually run.
// Checks are read-only; nothing is modified or started except tool version probes
// (added by the caller through `probe` so tests can run without a toolchain).

#include "cppsentinel/config/AppConfig.h"

#include <functional>
#include <string>
#include <vector>

namespace cppsentinel
{

struct DoctorCheck
{
    std::string name;
    bool ok = false;
    bool warning = false; ///< ok but worth attention
    std::string detail;
    std::string hint;
};

struct DoctorReport
{
    std::vector<DoctorCheck> checks;

    bool ok() const noexcept;
    bool hasWarnings() const noexcept;
};

/// Optional hook used to probe a tool executable (e.g. `<exe> --version`).
/// Returns an empty string when the probe is unavailable.
using ToolProbe = std::function<std::string(const std::string& checkerId,
                                            const EnvironmentProfile& environment,
                                            const std::string& executable)>;

DoctorReport runDoctor(const AppConfig& config, const ToolProbe& probe = {});

std::string formatDoctorText(const DoctorReport& report);
std::string formatDoctorJson(const DoctorReport& report);

} // namespace cppsentinel
