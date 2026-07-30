// Built-in checker factory registration (compile-time, no plugins).

#include "cppsentinel/core/CheckerRegistry.h"

#include "cppsentinel/checker/BuiltinCheckers.h"

namespace cppsentinel
{

void CheckerRegistry::registerFactory(std::string kind, CheckerFactory factory)
{
    factories_[std::move(kind)] = factory;
}

CheckerPtr CheckerRegistry::create(const CheckerConfig& config) const
{
    const auto it = factories_.find(config.kind);
    if (it == factories_.end() || !it->second)
    {
        return nullptr;
    }
    return it->second(config);
}

bool CheckerRegistry::contains(std::string_view kind) const
{
    return factories_.find(std::string(kind)) != factories_.end();
}

void registerBuiltinCheckers(CheckerRegistry& registry)
{
    registry.registerFactory("clang-tidy", &createClangTidyChecker);
    registry.registerFactory("clang-format", &createClangFormatChecker);
    registry.registerFactory("compiler-diagnostics",
                             &createCompilerDiagnosticsChecker);
    registry.registerFactory("cppcheck", &createCppcheckChecker);
}

} // namespace cppsentinel
