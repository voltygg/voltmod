#pragma once

// Plugin.cpp only: <CS2Kit/BuildInfo.hpp> is regenerated per commit (and only
// on plugin targets' include paths), so hoisting this into Api.hpp would
// rebuild every TU on every commit.

#include <CS2Kit/BuildInfo.hpp>
#include <CS2Kit/Core/MetamodPluginBase.hpp>

namespace CS2Kit::Core
{

/** @brief Your designated-init PluginInfo with Version/Date/Commit filled in from the
 *  build stamp; a typical call sets only Name, Author, Description and LogTag. */
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
