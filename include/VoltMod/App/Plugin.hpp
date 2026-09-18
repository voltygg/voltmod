#pragma once

#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Host/Abi.hpp>
#include <VoltMod/Host/HostTypes.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Host/IHostEvents.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Runtime.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Metadata returned by Plugin::Info(). BuildInfo.hpp supplies build identity fields. */
struct PluginInfo
{
    std::string Name = "VoltMod Plugin";
    std::string Author;
    std::string Description;
    std::string Url;
    std::string License = "MIT";
    std::string Version = "1.0.0";
    std::string Date = __DATE__;
    std::string Commit;
    std::string LogTag = "VoltMod";
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
    bool Attach(IHost& host, char* error, size_t errorSize);

    /** Reverse of Attach. Returns with nothing of this plugin's left running, which is what lets
     *  the host free the library. */
    void Detach();

    /** This plugin's status JSON, owned by it and valid until the next call. */
    const char* StatusJson();

protected:
    /** @brief Return metadata describing this plugin. */
    virtual PluginInfo Info() const = 0;

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

    /** Take the host's events in the order the framework used to install their hooks. */
    void SubscribeHostEvents();

    /** Release custom hooks, commands, plugin state, host events, then the runtime. */
    void Shutdown();

    // The host calls C function pointers, so each event arrives at a static that finds the plugin
    // through its context pointer. Every one of them stops exceptions at the boundary.
    static void OnHostFrame(void* context);
    static void OnHostServerStartup(void* context, HostString mapName);
    static void OnHostClientConnected(void* context, int slot, int64_t steamId, HostString name,
                                      HostString address);
    static void OnHostClientDisconnected(void* context, int slot);
    static void OnHostClientFullyConnected(void* context, int slot);
    static void OnHostClientSettingsChanged(void* context, int slot);
    static bool OnHostConsoleCommand(void* context, HostString name, HostString arguments, int slot);
    static void OnHostCheckTransmit(void* context, CCheckTransmitInfo** infoList, int infoCount);

    IHost* _host = nullptr;
    IHostEvents* _events = nullptr;

    // Reverse destruction order: custom hooks, host events, then their Runtime services.
    std::unique_ptr<Runtime> _runtime;
    Subscriptions _hostEvents;
    Subscriptions _customHooks;
    PluginInfo _info;  ///< captured at load, so the host can name the plugin after Detach
    std::string _status;
};

}  // namespace VoltMod

/**
 * @brief Define the plugin instance and the entry point the host resolves.
 *
 * Invoke once, at global namespace scope, in the plugin's Plugin.cpp. The plugin library is not a
 * Metamod plugin, so it defines KHook's dispatch pointer itself; Attach seeds it from the host.
 */
#define VOLTMOD_PLUGIN(PluginClass)                                                                \
    PluginClass g_##PluginClass;                                                                   \
    namespace KHook                                                                                \
    {                                                                                              \
    KHook::IKHook* __exported__khook = nullptr;                                                    \
    }                                                                                              \
    static bool VoltMod_PluginLoad(::VoltMod::IHost* host, char* error, size_t errorSize)          \
    {                                                                                              \
        return host && g_##PluginClass.Attach(*host, error, errorSize);                             \
    }                                                                                              \
    static void VoltMod_PluginUnload() { g_##PluginClass.Detach(); }                                \
    static const char* VoltMod_PluginStatus() { return g_##PluginClass.StatusJson(); }              \
    static const ::VoltMod::PluginDescriptor g_voltmodDescriptor{                                   \
        ::VoltMod::HostAbiVersion, &VoltMod_PluginLoad, &VoltMod_PluginUnload, &VoltMod_PluginStatus}; \
    extern "C" VOLTMOD_EXPORT const ::VoltMod::PluginDescriptor* VoltMod_PluginEntry()              \
    {                                                                                              \
        return &g_voltmodDescriptor;                                                                \
    }                                                                                               \
    static_assert(true, "VOLTMOD_PLUGIN requires a trailing semicolon")
