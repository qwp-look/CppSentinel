#pragma once

// Direct executable + argv launch for Linux and Windows native environments.

#include "cppsentinel/process/ProcessRunner.h"

namespace cppsentinel
{

class NativeProcessRunner final : public ProcessRunner
{
public:
    ProcessResult run(const ProcessRequest& request) const override;
};

} // namespace cppsentinel
