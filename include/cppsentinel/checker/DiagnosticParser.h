#pragma once

// Parse common tool log lines into Diagnostic values.

#include "cppsentinel/report/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace cppsentinel
{

/// Parse clang/gcc/clang-tidy style lines:
///   path:line:col: severity: message
///   path:line:col: severity: message [rule]
std::vector<Diagnostic> parseClangStyleDiagnostics(std::string_view text);

/// Parse cppcheck --template='{file}:{line}:{column}: {severity}: {message}
/// [{id}]' (same shape as clang-style for the template we pass).
std::vector<Diagnostic> parseCppcheckDiagnostics(std::string_view text);

Severity parseSeverityToken(std::string_view token);

} // namespace cppsentinel
