// Build child environment blocks from parent snapshot + profile overlays.

#include "cppsentinel/process/Environment.h"

#include "cppsentinel/process/PathUtil.h"

#include <boost/process/v2/environment.hpp>

#include <algorithm>

namespace cppsentinel
{
namespace
{

namespace bp = boost::process::v2;

std::string keyOf(const std::string& entry)
{
    const auto pos = entry.find('=');
    if (pos == std::string::npos)
    {
        return entry;
    }
    return entry.substr(0, pos);
}

} // namespace

std::vector<std::string> captureParentEnvironment()
{
    std::vector<std::string> env;
    env.reserve(128);
    for (auto entry : bp::environment::current())
    {
        env.push_back(entry.string());
    }
    return env;
}

void applyEnvOverrides(std::vector<std::string>& env,
                       const std::map<std::string, std::string>& overrides)
{
    for (const auto& [key, value] : overrides)
    {
        env.erase(std::remove_if(env.begin(), env.end(),
                                 [&](const std::string& item)
                                 { return iequals(keyOf(item), key); }),
                  env.end());
        std::string entry = key;
        entry.push_back('=');
        entry += value;
        env.push_back(std::move(entry));
    }
}

std::vector<std::string>
buildEnvironmentBlock(bool inheritParent, const EnvironmentProfile* profile,
                      const std::map<std::string, std::string>& extra)
{
    std::vector<std::string> env;
    // Both former branches did the same thing; the profile decides when
    // present.
    const bool inherit =
        profile != nullptr ? profile->inherit_parent : inheritParent;
    if (inherit)
    {
        env = captureParentEnvironment();
    }

    if (profile)
    {
        applyEnvOverrides(env, profile->env);
        if (profile->kind == EnvironmentKind::Msys2)
        {
            std::map<std::string, std::string> msys;
            msys["MSYSTEM"] =
                profile->msystem.empty() ? "MINGW64" : profile->msystem;
            msys["CHERE_INVOKING"] = "1";
            msys["MSYS2_PATH_TYPE"] = "inherit";
            applyEnvOverrides(env, msys);
        }
    }

    applyEnvOverrides(env, extra);
    return env;
}

} // namespace cppsentinel
