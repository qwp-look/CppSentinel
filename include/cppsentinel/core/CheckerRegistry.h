#pragma once

// Compile-time checker factory registry (no dynamic plugins in v2).

#include "cppsentinel/checker/Checker.h"
#include "cppsentinel/config/AppConfig.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace cppsentinel
{

/// Maps checker kind strings to factory functions returning
/// unique_ptr<Checker>.
class CheckerRegistry
{
  public:
    void registerFactory(std::string kind, CheckerFactory factory);

    /// Returns nullptr if kind is unknown.
    CheckerPtr create(const CheckerConfig& config) const;

    bool contains(std::string_view kind) const;

  private:
    std::unordered_map<std::string, CheckerFactory> factories_;
};

/// Register clang-tidy, clang-format, compiler-diagnostics, cppcheck.
void registerBuiltinCheckers(CheckerRegistry& registry);

} // namespace cppsentinel
