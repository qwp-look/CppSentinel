#pragma once

#include <string>

/// 外部工具进程的启动与等待结果。
struct RunResult
{
    bool launched = false; ///< 操作系统进程是否成功启动
    int exit_code = -1;    ///< 已启动时为子进程退出码；否则可为错误提示码
    std::string message;   ///< 可读状态或失败原因

    static RunResult success(int code)
    {
        RunResult r;
        r.launched = true;
        r.exit_code = code;
        r.message = "ok";
        return r;
    }

    static RunResult failure(const std::string& msg, int code = -1)
    {
        RunResult r;
        r.launched = false;
        r.exit_code = code;
        r.message = msg;
        return r;
    }

    /// 已启动且退出码为 0。
    bool ok() const
    {
        return launched && exit_code == 0;
    }
};
