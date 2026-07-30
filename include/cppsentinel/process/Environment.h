#pragma once

// Parent environment capture and KEY=VALUE block construction for child processes.

#include "cppsentinel/config/AppConfig.h"

#include <map>
#include <string>
#include <vector>

namespace cppsentinel
{

/// Snapshot of the current process environment as KEY=VALUE entries.
std::vector<std::string> captureParentEnvironment();

/// Build KEY=VALUE block: optional parent copy + profile overlays + extra overlays.
/// For Msys2 profiles also forces MSYSTEM / CHERE_INVOKING / MSYS2_PATH_TYPE.
std::vector<std::string>
buildEnvironmentBlock(bool inheritParent, const EnvironmentProfile* profile,
                      const std::map<std::string, std::string>& extra);

/// Replace matching keys (case-insensitive on Windows-style compares via iequals).
void applyEnvOverrides(std::vector<std::string>& env,
                       const std::map<std::string, std::string>& overrides);

} // namespace cppsentinel
