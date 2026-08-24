#pragma once

// Plugin.cpp only: <VoltMod/BuildInfo.hpp> is regenerated per commit (and only
// on plugin targets' include paths), so hoisting this into Api.hpp would
// rebuild every TU on every commit.

#include <VoltMod/App/MetamodPlugin.hpp>
#include <VoltMod/BuildInfo.hpp>

namespace VoltMod::App
{

/** @brief Your designated-init PluginInfo with Version/Date/Commit filled in from the
 *  build stamp; a typical call sets only Name, Author, Description and LogTag. */
inline PluginInfo WithBuildInfo(PluginInfo info)
{
    info.Version = VoltMod::BuildInfo::Version;
    info.Date = VoltMod::BuildInfo::BuildDate;
    info.Commit = VoltMod::BuildInfo::RepoCommit;
    return info;
}

}  // namespace VoltMod::App

namespace VoltMod
{
using App::WithBuildInfo;
}  // namespace VoltMod
