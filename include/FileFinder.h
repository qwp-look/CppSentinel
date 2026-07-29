#pragma once

#include <filesystem>
#include <optional>
#include <queue>
#include <string>
#include <utility>

namespace util
{

struct FindConfigResult
{
    std::filesystem::path path; ///< Configuration.toml 的路径
};

/// 广度优先查找名为 "Configuration.toml" 的文件。
///
/// @param rootPath      搜索起始目录
/// @param errorMessage  失败时写入原因
/// @param maxDepth      相对 root 的最大目录深度（0 表示只搜 root）
inline std::optional<FindConfigResult>
findConfigurationFile(const std::string& rootPath, std::string& errorMessage,
                      int maxDepth = 3)
{
    namespace fs = std::filesystem;

    if (rootPath.empty())
    {
        errorMessage = "Search path is empty";
        return std::nullopt;
    }

    const fs::path root(rootPath);
    std::error_code pathError;

    if (!fs::exists(root, pathError) || pathError)
    {
        errorMessage = "Search path does not exist: " + root.string();
        if (pathError)
        {
            errorMessage += " (" + pathError.message() + ")";
        }
        return std::nullopt;
    }

    if (!fs::is_directory(root, pathError) || pathError)
    {
        errorMessage = "Search path is not a directory: " + root.string();
        if (pathError)
        {
            errorMessage += " (" + pathError.message() + ")";
        }
        return std::nullopt;
    }

    std::queue<std::pair<fs::path, int>> pending;
    pending.emplace(root, 0);

    while (!pending.empty())
    {
        const auto [current, depth] = pending.front();
        pending.pop();

        // 使用 error_code 迭代，避免 try/catch 吞掉异常。
        std::error_code iterError;
        fs::directory_iterator it(
            current, fs::directory_options::skip_permission_denied, iterError);
        if (iterError)
        {
            continue;
        }

        const fs::directory_iterator end;
        for (; it != end; it.increment(iterError))
        {
            if (iterError)
            {
                // 当前目录无法继续遍历，跳到下一层候选项。
                break;
            }

            const auto& entry = *it;
            std::error_code entryError;
            if (entry.is_regular_file(entryError) && !entryError &&
                entry.path().filename() == "Configuration.toml")
            {
                FindConfigResult result;
                result.path = entry.path();
                return result;
            }

            if (depth < maxDepth && entry.is_directory(entryError) &&
                !entryError)
            {
                pending.emplace(entry.path(), depth + 1);
            }
        }
    }

    errorMessage = "Configuration.toml not found under '" + root.string() +
                   "' (maxDepth=" + std::to_string(maxDepth) + ")";
    return std::nullopt;
}

} // namespace util
