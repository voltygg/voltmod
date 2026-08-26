#pragma once

#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <cstddef>
#include <eiface.h>
#include <functional>
#include <icvar.h>
#include <memory>
#include <string_view>
#include <vector>

class ISource2WorldSession;

namespace VoltMod::Players
{
class Player;
}

namespace VoltMod
{
class Runtime;
}

namespace VoltMod::App
{

/** Plugin metadata returned by MetamodPlugin::Info(). Use BuildInfo.hpp for
 *  version, date, and commit fields when build identification matters. */
struct PluginInfo
{
    const char* Name = "VoltMod Plugin";
    const char* Author = "";
    const char* Description = "";
    const char* Url = "";
    const char* License = "MIT";
    const char* Version = "1.0.0";
    const char* Date = __DATE__;
    const char* Commit = "";
    const char* LogTag = "VoltMod";
};

/**
 * @brief Base class that owns Metamod integration, standard hooks, and players.
 *
 * The base creates one Runtime per load and passes it to OnLoad. Subclasses must
 * release their load-cycle state in OnUnload so `meta reload` starts clean.
 */
class MetamodPlugin : public ISmmPlugin, public IMetamodListener
{
public:
    MetamodPlugin();
    ~MetamodPlugin();

    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;

    const char* GetAuthor() override;
    const char* GetName() override;
    const char* GetDescription() override;
    const char* GetURL() override;
    const char* GetLicense() override;
    const char* GetVersion() override;
    const char* GetDate() override;
    const char* GetLogTag() override;

    void* OnMetamodQuery(const char* iface, int* ret) override;

protected:
    /** @brief Return your plugin's metadata. Called by the base to answer the Metamod info getters. */
    virtual PluginInfo Info() const = 0;

    /**
     * @brief Build load-cycle state, load configuration, and register commands.
     * Return false to abort the load.
     * @param late true if the plugin was loaded after the server had already started.
     */
    virtual bool OnLoad(Runtime& runtime, bool late) = 0;

    /** @brief Release state created by OnLoad before the runtime is destroyed. */
    virtual void OnUnload() {}

    /**
     * @brief Called at each map start after game-event listeners are attached.
     * The engine resets convars and runs game-mode cfgs around this callback, so
     * plugins may need to reapply load-time values.
     */
    virtual void OnServerStartup(const char* mapName) {}

    /** @brief A player joined and is now tracked. @p player is valid (read its SteamID, name, etc.). */
    virtual void OnPlayerConnect(Players::Player* player) {}

    /** @brief A player is leaving, still tracked for this call. @p player may be null - check it. */
    virtual void OnPlayerDisconnect(Players::Player* player) {}

    /**
     * @brief A player finished connecting and is now in the server (post ClientFullyConnect) -
     * the first point their name and convars are meaningful. @p player may be null - check it.
     */
    virtual void OnPlayerFullyConnected(Players::Player* player) {}

    /**
     * @brief A player changed a replicated setting (name, userinfo cvars). Fires on every
     * change, including the ones the engine sends at connect. @p player may be null - check it.
     */
    virtual void OnPlayerSettingsChanged(Players::Player* player) {}

    /**
     * @brief A player sent a `say` or `say_team` message.
     *
     * The default consumes pending chat input, then dispatches registered `!` and
     * `.` commands. An override replaces that behavior and must consume input
     * itself when the plugin uses menu prompts.
     *
     * @return true to swallow it (the message won't appear in chat), false to let it through.
     */
    virtual bool OnPlayerChat(Players::Player* player, std::string_view message, bool teamChat);

    /** @brief Install your own SourceHook hooks here; the base already installs the common ones.
     *  Keep each VOLTMOD_SCOPED_HOOK subscription in a member so it is removed on unload. */
    virtual void OnRegisterHooks(Runtime& runtime) {}

    /** @brief The live runtime, for hook bodies. Valid between OnLoad and OnUnload. */
    Runtime& Rt() { return *_runtime; }

    /** @brief True if the plugin was loaded after the server started, rather than at boot. */
    bool IsLateLoad() const { return _lateLoad; }

    // Standard hook callbacks; subclasses use the virtual callbacks above.
    void Hook_GameFrame(bool simulating, bool firstTick, bool lastTick);
    void Hook_StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession* session,
                            const char* mapName);
    void Hook_OnClientConnected(CPlayerSlot slot, const char* name, uint64 xuid, const char* networkId,
                                const char* address, bool fakePlayer);
    void Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* name, uint64 xuid,
                               const char* networkId);
    void Hook_ClientFullyConnect(CPlayerSlot slot);
    void Hook_ClientSettingsChanged(CPlayerSlot slot);
    void Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args);
    void Hook_CheckTransmit(CCheckTransmitInfo** infoList, int infoCount, CBitVec<16384>& unionTransmitEdicts,
                            CBitVec<16384>& unused, const Entity2Networkable_t** networkables,
                            const uint16* entityIndices, int entityCount);

private:
    void RegisterStandardHooks();

    bool _lateLoad = false;
    /** Release plugin state, standard hooks, and runtime in that order. */
    void Shutdown();

    // Runtime first so implicit destruction also removes the hooks before the services they
    // call into go away - the same order Shutdown() enforces explicitly.
    std::unique_ptr<VoltMod::Runtime> _runtime;
    std::vector<Core::Subscription> _standardHooks;
    PluginInfo _info;  // cached copy of Info() captured at load; backs the ISmmPlugin getters
};

}  // namespace VoltMod::App

/**
 * @brief Define the global plugin instance and Metamod entry point.
 *
 * Invoke once, at global namespace scope, in the plugin's Plugin.cpp.
 */
#define VOLTMOD_PLUGIN(PluginClass)              \
    PluginClass g_##PluginClass;                 \
    PLUGIN_EXPOSE(PluginClass, g_##PluginClass); \
    static_assert(true, "VOLTMOD_PLUGIN requires a trailing semicolon")
