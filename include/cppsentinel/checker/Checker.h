#pragma once

// Checker interface: config + context in, CheckerResult out. No console I/O.

#include "cppsentinel/checker/CheckerContext.h"
#include "cppsentinel/report/Types.h"

#include <memory>
#include <string>
#include <string_view>

namespace cppsentinel
{

/// One audit tool integration. run() is const; do not store per-run mutable state.
class Checker
{
public:
    virtual ~Checker() = default;

    /// Registry key, e.g. "clang-tidy" (matches [[checker]].kind).
    virtual std::string_view kind() const noexcept = 0;

    /// Build ProcessRequest, run via context.runner, parse logs, return result.
    virtual CheckerResult run(const CheckerContext& context) const = 0;
};

using CheckerPtr = std::unique_ptr<Checker>;
using CheckerFactory = CheckerPtr (*)(const CheckerConfig& config);

} // namespace cppsentinel
