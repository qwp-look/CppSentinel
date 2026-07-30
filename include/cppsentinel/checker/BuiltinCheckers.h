#pragma once

// Factory entry points for built-in checkers (registered in CheckerRegistry).

#include "cppsentinel/checker/Checker.h"
#include "cppsentinel/config/AppConfig.h"

namespace cppsentinel
{

CheckerPtr createClangTidyChecker(const CheckerConfig& config);
CheckerPtr createClangFormatChecker(const CheckerConfig& config);
CheckerPtr createCompilerDiagnosticsChecker(const CheckerConfig& config);
CheckerPtr createCppcheckChecker(const CheckerConfig& config);

} // namespace cppsentinel
