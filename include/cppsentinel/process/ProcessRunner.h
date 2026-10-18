#pragma once

// Abstract process launcher used by checkers (Native / MSYS2 / Fake).

#include "cppsentinel/process/ProcessTypes.h"

#include <memory>

namespace cppsentinel
{

class ProcessRunner
{
  public:
    virtual ~ProcessRunner() = default;

    /// Launch request.executable with request.arguments; capture logs +
    /// timeout.
    virtual ProcessResult run(const ProcessRequest& request) const = 0;
};

using ProcessRunnerPtr = std::shared_ptr<const ProcessRunner>;

} // namespace cppsentinel
