#pragma once

// v2 Configuration.toml loader and validator.

#include "cppsentinel/config/AppConfig.h"
#include "cppsentinel/config/ConfigDiagnostic.h"

#include <filesystem>

namespace cppsentinel
{

/// Load and validate a v2 Configuration.toml.
///
/// - Relative paths resolve against the config file directory.
/// - Collects all validation errors before returning (ok == false).
/// - Preserves [[checker]] declaration order.
/// - Does not accept legacy Method tables or command shell templates.
ConfigLoadResult loadConfig(const std::filesystem::path& configPath);

} // namespace cppsentinel
