#pragma once

// Include from the plugin's Plugin.cpp only: <CS2Kit/BuildInfo.hpp> is
// regenerated per commit and sits on plugin targets' include paths
// (cs2kit_stamp_build_info) - the kit itself never compiles it, and putting
// this in Api.hpp would rebuild every TU on every commit.

#include <CS2Kit/BuildInfo.hpp>
#include <CS2Kit/Core/MetamodPluginBase.hpp>

namespace CS2Kit::Core
{

/** @brief Your designated-init PluginInfo with Version/Date/Commit overwritten
 *  from the build stamp. License already defaults to "MIT", so a typical call
 *  sets only Name, Author, Description, and LogTag. */
inline PluginInfo WithBuildInfo(PluginInfo info)
{
    info.Version = CS2Kit::BuildInfo::Version;
    info.Date = CS2Kit::BuildInfo::BuildDate;
    info.Commit = CS2Kit::BuildInfo::RepoCommit;
    return info;
}

}  // namespace CS2Kit::Core

namespace CS2Kit
{
using Core::WithBuildInfo;
}  // namespace CS2Kit
