#pragma once

// Plugin.cpp only: <CS2Kit/BuildInfo.hpp> is regenerated per commit (and only
// on plugin targets' include paths), so hoisting this into Api.hpp would
// rebuild every TU on every commit.

#include <CS2Kit/App/MetamodPlugin.hpp>
#include <CS2Kit/BuildInfo.hpp>

namespace CS2Kit::App
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

}  // namespace CS2Kit::App

namespace CS2Kit
{
using App::WithBuildInfo;
}  // namespace CS2Kit
