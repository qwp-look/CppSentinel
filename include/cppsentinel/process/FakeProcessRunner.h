#pragma once

// Test double: records ProcessRequests and returns scripted ProcessResults.

#include "cppsentinel/process/ProcessRunner.h"

#include <functional>
#include <vector>

namespace cppsentinel
{

class FakeProcessRunner final : public ProcessRunner
{
  public:
    using Handler = std::function<ProcessResult(const ProcessRequest&)>;

    void setHandler(Handler handler);
    void enqueue(ProcessResult result);

    const std::vector<ProcessRequest>& requests() const noexcept
    {
        return requests_;
    }

    ProcessResult run(const ProcessRequest& request) const override;

  private:
    mutable std::vector<ProcessRequest> requests_;
    mutable std::vector<ProcessResult> queue_;
    Handler handler_;
};

} // namespace cppsentinel
