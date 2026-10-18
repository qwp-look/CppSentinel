#include "cppsentinel/core/BuiltinTools.h"

namespace cppsentinel
{

std::string defaultExecutableForKind(const std::string& kind)
{
    if (kind == "clang-tidy")
    {
        return "clang-tidy";
    }
    if (kind == "clang-format")
    {
        return "clang-format";
    }
    if (kind == "cppcheck")
    {
        return "cppcheck";
    }
    if (kind == "compiler-diagnostics")
    {
        return "c++";
    }
    return {};
}

bool checkerNeedsCompilationDatabase(const std::string& kind)
{
    return kind == "clang-tidy" || kind == "compiler-diagnostics";
}

} // namespace cppsentinel
