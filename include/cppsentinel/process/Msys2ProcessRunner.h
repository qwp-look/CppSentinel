#pragma once

// Windows-only MSYS2 runner: bash -l -c 'exec "$@"' with MSYSTEM selection.

#include "cppsentinel/config/AppConfig.h"
#include "cppsentinel/process/ProcessRunner.h"

#include <filesystem>
#include <optional>
#include <string>

namespace cppsentinel
{

struct Msys2Install
{
    std::filesystem::path root; ///< e.g. C:\msys64
    std::filesystem::path bash; ///< root/usr/bin/bash.exe
};

/// Locate MSYS2: profile.root, then MSYS2_ROOT / MSYS2_PATH, then common
/// defaults.
std::optional<Msys2Install> findMsys2Install(const EnvironmentProfile* profile,
                                             std::string& errorMessage);

/// Converts Windows paths at the boundary; does not join argv into a shell
/// string.
class Msys2ProcessRunner final : public ProcessRunner
{
  public:
    explicit Msys2ProcessRunner(EnvironmentProfile profile);

    ProcessResult run(const ProcessRequest& request) const override;

  private:
    EnvironmentProfile profile_;
};

} // namespace cppsentinel
