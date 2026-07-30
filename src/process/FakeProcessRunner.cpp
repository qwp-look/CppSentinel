// In-memory ProcessRunner for Engine unit tests (no real subprocesses).

#include "cppsentinel/process/FakeProcessRunner.h"

namespace cppsentinel
{

void FakeProcessRunner::setHandler(Handler handler)
{
    handler_ = std::move(handler);
}

void FakeProcessRunner::enqueue(ProcessResult result)
{
    queue_.push_back(std::move(result));
}

ProcessResult FakeProcessRunner::run(const ProcessRequest& request) const
{
    requests_.push_back(request);
    if (handler_)
    {
        return handler_(request);
    }
    if (!queue_.empty())
    {
        ProcessResult result = std::move(queue_.front());
        queue_.erase(queue_.begin());
        if (result.stdout_log.empty())
        {
            result.stdout_log = request.stdout_log;
        }
        if (result.stderr_log.empty())
        {
            result.stderr_log = request.stderr_log;
        }
        return result;
    }

    ProcessResult result;
    result.state = ProcessState::StartError;
    result.error_message = "FakeProcessRunner has no queued result";
    result.stdout_log = request.stdout_log;
    result.stderr_log = request.stderr_log;
    return result;
}

} // namespace cppsentinel
