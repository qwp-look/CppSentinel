#pragma once

#include <string>

/// 从 Configuration.toml 加载的一条审计/检查配置。
///
/// TOML 字段映射：
///   enabled     -> enabled
///   name        -> name（缺省时用表名）
///   environment -> environment（mingw64 / clang64 / ...）
///   command     -> command（可含 {file_in} / {file_out}）
struct Method
{
    bool enabled = false;    ///< 是否启用
    std::string name;        ///< 显示名 / 输出文件名
    std::string environment; ///< MSYS2 环境名
    std::string command;     ///< 命令模板
};
