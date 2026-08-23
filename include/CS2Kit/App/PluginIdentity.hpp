#pragma once

#include <CS2Kit/Core/PluginManifest.hpp>
#include <string>

namespace CS2Kit::App
{

/**
 * @brief Holds this plugin's manifest, announces it to peers, reports unmet dependencies.
 *
 * Owned by Services, so the published pointer lives exactly one load cycle.
 *
 * The dependency report is advisory and runs on the first game frame, not at OnLoad: .vdf
 * load order is Metamod's, so during load a peer that comes later looks exactly like one
 * that is missing. Callers still handle a null from ServiceExchange::Get, which stays
 * correct whenever a peer arrives or leaves.
 */
class PluginIdentity final : public Core::IPluginIdentity
{
public:
    /** Take @p manifest, publish it, and queue the dependency report. */
    void Adopt(Core::PluginManifest manifest);
    /** Stop answering peer lookups. Called from Shutdown. */
    void Withdraw();

    /** What this plugin declared; empty when it ships no manifest. */
    const Core::PluginManifest& Manifest() const { return _manifest; }

    const char* PluginVersion() const override { return _manifest.Version.c_str(); }

private:
    Core::PluginManifest _manifest;
    std::string _key;
};

}  // namespace CS2Kit::App
