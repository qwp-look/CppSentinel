#pragma once

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Method.h"
#include "toml.hpp"

namespace util
{

/// 将 Configuration.toml 解析为 Method 列表。
///
/// 硬错误（IO、TOML 语法、空文件）返回 false 并设置 errorMessage。
/// 软问题（enabled 但缺 command）仅打印警告。
inline bool parseMethodConfiguration(const std::filesystem::path& tomlPath,
                                     std::vector<Method>& methodConfiguration,
                                     std::string& errorMessage)
{
    methodConfiguration.clear();

    try
    {
        const auto config = toml::parse_file(tomlPath.string());

        for (auto&& [key, value] : config)
        {
            if (!value.is_table())
            {
                continue;
            }

            auto& table = *value.as_table();
            Method method;

            if (auto name = table["name"].value<std::string>())
            {
                method.name = *name;
            }
            else
            {
                method.name = std::string(key.str());
            }

            if (auto enabled = table["enabled"].value<bool>())
            {
                method.enabled = *enabled;
            }
            else
            {
                method.enabled = false;
            }

            if (auto env = table["environment"].value<std::string>())
            {
                method.environment = *env;
            }

            if (auto cmd = table["command"].value<std::string>())
            {
                method.command = *cmd;
            }

            if (method.enabled)
            {
                if (method.command.empty())
                {
                    std::cerr << "[config] 警告: method '" << method.name
                              << "' 已启用但 command 为空，运行时会失败\n";
                }
                if (method.environment.empty())
                {
                    std::cerr << "[config] 警告: method '" << method.name
                              << "' 已启用但 environment 为空，运行时会失败\n";
                }
            }

            methodConfiguration.push_back(std::move(method));
        }

        if (methodConfiguration.empty())
        {
            errorMessage =
                "配置文件中未找到任何 method 表: " + tomlPath.string();
            return false;
        }

        return true;
    }
    catch (const toml::parse_error& e)
    {
        errorMessage = std::string("TOML 解析失败 (") + tomlPath.string() +
                       "): " + e.what();
        return false;
    }
    catch (const std::exception& e)
    {
        errorMessage = std::string("读取配置失败 (") + tomlPath.string() +
                       "): " + e.what();
        return false;
    }
}

} // namespace util
