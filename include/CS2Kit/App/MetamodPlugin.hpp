#pragma once

#include <CS2Kit/Core/MetamodGlobals.hpp>
#include <CS2Kit/Core/Subscription.hpp>
#include <cstddef>
#include <eiface.h>
#include <functional>
#include <icvar.h>
#include <memory>
#include <string_view>
#include <vector>

class ISource2WorldSession;

namespace CS2Kit::Players
{
class Player;
}

namespace CS2Kit
{
class Runtime;
}

namespace CS2Kit::App
{

/** @brief Your plugin's name, author, version, and log tag. Return it from MetamodPlugin::Info().
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
 * you care about.
 *
 * The base owns the Runtime for exactly one Load/Unload cycle and hands it to OnLoad. Your
 * plugin owns everything else the same way - build the object graph in OnLoad, drop it in
 * OnUnload - so nothing survives a `meta reload`, and each service is handed the collaborators
 * it needs instead of reaching for them.
 *
 * @code
 * class MyPlugin final : public CS2Kit::MetamodPlugin {
 *     PluginInfo Info() const override { return {.Name = "My Plugin", .LogTag = "MINE"}; }
 *     bool OnLoad(CS2Kit::Runtime& runtime, bool late) override
 *     {
 *         _app.emplace(runtime);
 *         return _app->Start();
 *     }
 *     void OnUnload() override { _app.reset(); }
 *     std::optional<MySystem::App> _app;
 * };
 * CS2KIT_PLUGIN(MyPlugin);
 * @endcode
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
     * @brief Set up your plugin here: build its object graph over @p runtime, load config,
     * register commands. Construct whatever holds load-cycle state now and release it in
     * OnUnload - that pairing is the whole lifetime.
     * Return false to abort the load.
     * @param late true if the plugin was loaded after the server had already started.
     */
    virtual bool OnLoad(Runtime& runtime, bool late) = 0;

    /** @brief Release whatever OnLoad built, before the runtime goes away. */
    virtual void OnUnload() {}

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
     * The default first hands the line to any pending @ref CS2Kit::Sdk::ChatInputCapture
     * prompt for that slot, then dispatches registered chat commands ("!x" / ".x") and
     * swallows handled ones; anything else, including unknown "!words", falls through to
     * normal chat. An override replaces all of that wholesale - if your plugin uses menu
     * input rows, call `runtime.ChatInput.TryConsume` first or the prompt never completes.
     *
     * @return true to swallow it (the message won't appear in chat), false to let it through.
     */
    virtual bool OnPlayerChat(Players::Player* player, std::string_view message, bool teamChat);

    /** @brief Install your own SourceHook hooks here; the base already installs the common ones.
     *  Keep each CS2KIT_SCOPED_HOOK subscription in a member so it is removed on unload. */
    virtual void OnRegisterHooks(Runtime& runtime) {}

    /** @brief The live runtime, for hook bodies. Valid between OnLoad and OnUnload. */
    Runtime& Rt() { return *_runtime; }

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

    bool _lateLoad = false;
    /** Drop the plugin graph, the standard hooks and the runtime, in that order.
     *  Shared by Unload and the failed-OnLoad path so neither can drift. */
    void Teardown();

    std::vector<Core::Subscription> _standardHooks;
    std::unique_ptr<CS2Kit::Runtime> _runtime;
    PluginInfo _info;  // cached copy of Info() captured at load; backs the ISmmPlugin getters
};

}  // namespace CS2Kit::App

/**
 * @brief The per-plugin entry-point boilerplate in one statement: the global instance
 * (g_<PluginClass>) and Metamod's PLUGIN_EXPOSE.
 *
 * Invoke once, at global namespace scope, in the plugin's Plugin.cpp.
 */
#define CS2KIT_PLUGIN(PluginClass)               \
    PluginClass g_##PluginClass;                 \
    PLUGIN_EXPOSE(PluginClass, g_##PluginClass); \
    static_assert(true, "CS2KIT_PLUGIN requires a trailing semicolon")
