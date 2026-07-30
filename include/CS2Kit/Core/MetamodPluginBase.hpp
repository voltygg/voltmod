#pragma once

#include <ISmmPlugin.h>

#include <cstddef>
#include <eiface.h>
#include <functional>
#include <icvar.h>
#include <memory>
#include <string_view>
#include <vector>

class ISource2WorldSession;

// Externs for the SourceHook globals every plugin TU references (SH_* macros,
// RETURN_META); the definitions come from CS2KIT_PLUGIN / PLUGIN_EXPOSE. Pure
// externs, so a plugin repeating PLUGIN_GLOBALVARS() itself is harmless.
PLUGIN_GLOBALVARS();

namespace CS2Kit::Players
{
class Player;
}

namespace CS2Kit::Core
{

class Services;

/** @brief Your plugin's name, author, version, and log tag. Return it from MetamodPluginBase::Info().
 *  Wire Version/Date/Commit to <CS2Kit/BuildInfo.hpp> so `meta list` shows the exact build. */
struct PluginInfo
{
    const char* Name = "CS2Kit Plugin";
    const char* Author = "";
    const char* Description = "";
    const char* Url = "";
    const char* License = "MIT";
    const char* Version = "1.0.0";
    const char* Date = __DATE__;
    const char* Commit = "";
    const char* LogTag = "CS2Kit";
};

/**
 * @brief Base class for a Metamod:Source plugin - handles the boilerplate so you don't have to.
 *
 * Subclassing this gets you a working plugin: it wires up the engine, registers the common
 * hooks (game frame, player connect/disconnect, chat), and tracks connected players for you.
 * You provide the metadata via Info(), your setup in OnLoad(), and override only the callbacks
 * you care about. Managers-carrying plugins use PluginBase<TManagers> + CS2KIT_PLUGIN instead
 * of the instance/PLUGIN_EXPOSE pair below.
 *
 * @code
 * class MyPlugin : public CS2Kit::Core::MetamodPluginBase {
 *     PluginInfo Info() const override { return { .Name = "My Plugin", .LogTag = "MINE" }; }
 *     bool OnLoad(bool late) override { Defer([]{ MySystem::Shutdown(); }); return MySystem::Init(); }
 * };
 * MyPlugin g_MyPlugin;
 * PLUGIN_EXPOSE(MyPlugin, g_MyPlugin);
 * @endcode
 */
class MetamodPluginBase : public ISmmPlugin, public IMetamodListener
{
public:
    MetamodPluginBase();
    ~MetamodPluginBase();

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
     * @brief Set up your plugin here - load config, connect services, register commands.
     * Return false to abort the load; the base then runs your Defer() cleanups and shuts down.
     * @param late true if the plugin was loaded after the server had already started.
     */
    virtual bool OnLoad(bool late) = 0;

    /** @brief Optional extra teardown on unload. Prefer Defer() - it runs automatically. */
    virtual void OnUnload() {}

    /**
     * @brief Construct plugin-owned service instances (your Managers container). Runs during
     * Load after the kit services are initialized and hooks are registered, right before
     * OnLoad - so member initializers may call Engine(). Prefer @ref PluginBase, which
     * overrides this (and OnDestroyInstances) for you.
     */
    virtual void OnCreateInstances() {}

    /**
     * @brief Destroy plugin-owned service instances. Runs on unload AFTER the Defer() cleanups
     * (so deferred teardown still sees live instances) and BEFORE the kit's own services are
     * destroyed. Override to `reset()` your Managers container.
     */
    virtual void OnDestroyInstances() {}

    /**
     * @brief The engine is starting a (new) map. Fires on every map start, after the engine has
     * loaded game event definitions - by this point kit game-event listeners are attached. Note
     * that the engine resets game convars and re-execs gamemode cfgs around this, so values set
     * at load time may need re-asserting from here or from a game event.
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
     * @brief A player sent a chat message (say / say_team).
     *
     * The default dispatches registered chat commands ("!x" / ".x") and swallows handled
     * ones; anything else, including unknown "!words", falls through to normal chat. An
     * override replaces that dispatch wholesale.
     *
     * @return true to swallow it (the message won't appear in chat), false to let it through.
     */
    virtual bool OnPlayerChat(Players::Player* player, std::string_view message, bool teamChat);

    /** @brief Install your own SourceHook hooks here; the base already installs the common ones.
     *  Pair each with a Defer() that removes it. */
    virtual void OnRegisterHooks() {}

    /**
     * @brief Queue a cleanup callback to run when the plugin unloads (or if loading fails).
     * Deferred callbacks run in reverse order, so register each cleanup right next to its setup.
     */
    void Defer(std::function<void()> cleanup);

    /** @brief True if the plugin was loaded after the server started, rather than at boot. */
    bool IsLateLoad() const { return _lateLoad; }

    // The base's own callbacks for the standard hooks. They forward to the virtuals above -
    // you don't call these directly.
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
    void RunDeferred();

    bool _lateLoad = false;
    std::vector<std::function<void()>> _deferred;
    std::unique_ptr<Services> _services;
    PluginInfo _info;  // cached copy of Info() captured at load; backs the ISmmPlugin getters
};

}  // namespace CS2Kit::Core
