#pragma once

#include "Host/Plugins/HostView.hpp"

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The loaded plugins and the engine events raised on them, one per process.
 *
 * Owns the @ref HostState every plugin's view reaches through. Game thread only, like everything
 * a plugin reaches; nothing here locks. Free of the SDK so it is unit-tested without the engine -
 * @p metamod, @p hookDispatcher and @p gameData are values it hands on unchanged.
 */
class PluginHost
{
public:
    explicit PluginHost(SourceMM::ISmmAPI* metamod = nullptr, KHook::IKHook* hookDispatcher = nullptr,
                        IHostGameData* gameData = nullptr);
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    /** Give @p name its own view, at the end of the dispatch order. Nullptr when it already has one.
     *  @p logTag prefixes every line the plugin writes; empty means its name. */
    HostView* AddPlugin(std::string_view name, std::string_view logTag = {});

    /** Drop every subscription, publication and command name @p name still holds, and report them. */
    Unreleased RemovePlugin(std::string_view name);

    HostView* FindPlugin(std::string_view name);

    /** The one gamedata resolution every plugin binds from, null when the host has none. */
    IHostGameData* GameData() const { return _state.GameData; }

    /** Record what the host's own schema check found. @p stamp identifies the layout it checked,
     *  so a plugin can tell whether the answer covers the offsets it was built with. */
    void SetSchemaLayout(uint64_t stamp, bool verified);

    void RaiseFrame();
    void RaiseServerStartup(std::string_view mapName);
    void RaiseClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address);
    void RaiseClientDisconnected(int slot);
    void RaiseClientFullyConnected(int slot);
    void RaiseClientSettingsChanged(int slot);
    /** True when a plugin answered it: later plugins never see it and the engine call is blocked. */
    bool RaiseConsoleCommand(std::string_view name, std::string_view arguments, int slot);
    void RaiseCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

    /** The plugin holding @p name for its console commands, empty while the name is free. */
    std::string_view CommandOwner(std::string_view name) const;

    /** Keep @p name for the host's own console commands, so no plugin can take it. */
    void RegisterHostCommand(std::string_view name);

private:
    HostState _state;
    std::vector<std::unique_ptr<HostView>> _plugins;  ///< in load order
};

}  // namespace VoltMod
