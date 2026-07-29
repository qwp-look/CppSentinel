#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Method.h"
#include "RunResult.h"

namespace util
{

/// 一次 scheduleMethods() 运行后的汇总计数。
struct ScheduleSummary
{
    int total = 0;     ///< 配置中的 method 总数（含禁用）
    int skipped = 0;   ///< 已禁用而跳过
    int launched = 0;  ///< 操作系统进程成功启动
    int succeeded = 0; ///< 已启动且 exit_code == 0
    int failed = 0;    ///< 未启动、非 0 退出、或配置错误
};

/// 调度输入路径，避免相邻 path 参数易被调换。
struct SchedulePaths
{
    std::filesystem::path checkPath; ///< 替换到 {file_in}
    std::filesystem::path reportDir; ///< 报告输出根目录
};

/// 按顺序运行全部 method。
/// 每个已启用项通过 {file_out} 写入 reportDir/<Name>.out。
/// 单个 method 失败不会中断后续队列。
ScheduleSummary scheduleMethods(const std::vector<Method>& methods,
                                const SchedulePaths& paths);

} // namespace util
