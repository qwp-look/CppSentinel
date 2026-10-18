#pragma once

// Lightweight compile_commands.json helpers for clang-tidy /
// compiler-diagnostics.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cppsentinel
{

struct CompileCommand
{
    std::filesystem::path directory;
    std::filesystem::path file;
    std::vector<std::string> arguments; ///< argv style when present
    std::string command;                ///< raw command string fallback
};

/// Best-effort parse of compile_commands.json (no full JSON library
/// dependency).
std::vector<CompileCommand>
loadCompileCommands(const std::filesystem::path& path,
                    std::string& errorMessage);

/// Prefer absolute path match; fall back to filename match.
std::optional<CompileCommand>
findCompileCommand(const std::vector<CompileCommand>& entries,
                   const std::filesystem::path& target);

/// Split a shell-ish command line into argv (supports simple quotes).
std::vector<std::string> splitCommandLine(const std::string& command);

/// Drop compiler binary / -c / -o / depfile flags; ensure -fsyntax-only +
/// target.
std::vector<std::string>
syntaxOnlyArgsFromCompileCommand(const CompileCommand& entry,
                                 const std::filesystem::path& target);

} // namespace cppsentinel
