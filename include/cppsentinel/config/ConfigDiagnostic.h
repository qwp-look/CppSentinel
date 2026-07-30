#pragma once

// Structured configuration errors collected by ConfigLoader (no direct cerr).

#include "cppsentinel/config/AppConfig.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppsentinel
{

/// One validation or parse problem; CLI prints the full list on load failure.
struct ConfigDiagnostic
{
    std::string path;    ///< Field path, e.g. checker[0].id
    std::string message; ///< What is wrong
    std::string hint;    ///< How to fix
    std::optional<std::uint32_t> line;
    std::optional<std::uint32_t> column;
};

/// Result of loadConfig(): either ok+config or a non-empty diagnostics list.
struct ConfigLoadResult
{
    bool ok = false;
    AppConfig config;
    std::vector<ConfigDiagnostic> diagnostics;

    explicit operator bool() const noexcept
    {
        return ok;
    }
};

} // namespace cppsentinel
