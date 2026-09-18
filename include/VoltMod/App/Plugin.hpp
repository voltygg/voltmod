#pragma once

#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Runtime.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** The build stamp VOLTMOD_PLUGIN takes from the generated BuildInfo.hpp. */
struct PluginBuild
{
    const char* Version = "";
    const char* Commit = "";
    const char* Date = "";
};

/**
 * @brief Owns one Runtime per load cycle and the plugin's subscriptions to the host's events.
 *
 * OnLoad receives a fresh Runtime. Release plugin-owned state in OnUnload so `volt reload` starts
 * with no references to the previous cycle.
 */
class Plugin
{
public:
    Plugin();
    virtual ~Plugin();

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    /** Called by VOLTMOD_PLUGIN's entry point, not by plugin code. */
    bool Attach(IHost& host, const PluginBuild& build, char* error, size_t errorSize);

    /** Reverse of Attach. Returns with nothing of this plugin's left running, which is what lets
     *  the host free the library. */
    void Detach();

    /** This plugin's status JSON, owned by it and valid until the next call. */
    const char* StatusJson();

protected:
    /**
     * @brief Build load-cycle state, load configuration, and register commands.
     * @return false to abort the load.
     */
    virtual bool OnLoad(Runtime& runtime) = 0;

    /** @brief Release state created by OnLoad before the runtime is destroyed. */
    virtual void OnUnload() {}

    /**
     * @brief Called at each map start after game-event listeners are attached.
     * Reapply load-time convar values here because the engine runs game-mode cfgs around it.
     */
    virtual void OnServerStartup(std::string_view mapName) {}

    /**
     * @brief A player sent a `say` or `say_team` message.
     *
     * The default consumes pending menu input, then dispatches registered `!` and `.` commands.
     * An override must consume menu prompts itself.
     *
     * @return true to swallow it (the message won't appear in chat), false to let it through.
     */
    virtual bool OnPlayerChat(Player* player, std::string_view message, bool teamChat);

    /**
     * @brief Register custom hooks in @p hooks.
     *
     * The base clears @p hooks before OnUnload, preventing callbacks into state being released.
     */
    virtual void OnRegisterHooks(Runtime& runtime, Subscriptions& hooks) {}

private:
    /** Republish the entity system and the per-map state, then tell the plugin. */
    void HandleServerStartup(std::string_view mapName);

    /** Route a say/say_team line to OnPlayerChat and a ballot to the vote hook.
     *  @return true when this plugin answered it, which keeps it from the game's own handler. */
    bool HandleConsoleCommand(std::string_view name, std::string_view arguments, int slot);

    void SubscribeHostEvents();

    /** Release custom hooks, commands, plugin state, host events, then the runtime. */
    void Shutdown();

    void HostFrame();
    void HostClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address);
    void HostClientDisconnected(int slot);
    void HostClientFullyConnected(int slot);
    void HostClientSettingsChanged(int slot);
    void HostCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

    IHost* _host = nullptr;

    // Reverse destruction order: custom hooks, host events, then their Runtime services.
    std::unique_ptr<Runtime> _runtime;
    Subscriptions _hostEvents;
    Subscriptions _customHooks;
    std::string _status;
};

}  // namespace VoltMod
