#pragma once

#include <VoltMod/App/PluginIdentity.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/ILogger.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/PluginPolicy.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Sdk/ChatInputCapture.hpp>
#include <VoltMod/Sdk/ClientCvarService.hpp>
#include <VoltMod/Sdk/ConVarService.hpp>
#include <VoltMod/Sdk/Entity.hpp>
#include <VoltMod/Sdk/EntityOps.hpp>
#include <VoltMod/Sdk/GameData.hpp>
#include <VoltMod/Sdk/GameEventService.hpp>
#include <VoltMod/Sdk/GameInterfaces.hpp>
#include <VoltMod/Sdk/InputHistoryService.hpp>
#include <VoltMod/Sdk/MovementHook.hpp>
#include <VoltMod/Sdk/NetChannel.hpp>
#include <VoltMod/Sdk/PrecacheService.hpp>
#include <VoltMod/Sdk/TeleportTracker.hpp>
#include <VoltMod/Sdk/TransmitFilter.hpp>
#include <VoltMod/Sdk/UserMessage.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace SourceMM
{
class ISmmAPI;
}
using SourceMM::ISmmAPI;

namespace VoltMod::Sdk
{
class SchemaService;  // internal (src/Sdk/Schema.hpp) - by pointer, so this header stays clean
}

namespace VoltMod
{

/** Everything @ref Runtime::Start needs from Metamod, plus the optional overrides. */
struct LoadContext
{
    ISmmAPI* Ismm = nullptr;             ///< Metamod API pointer, from Plugin::Load
    char* Error = nullptr;               ///< Error buffer Metamod shows if the load fails
    size_t MaxLen = 0;                   ///< Size of that buffer
    bool Late = false;                   ///< Loaded after the server had already started
    const char* LogPrefix = "VoltMod";   ///< Console log prefix, e.g. "[ADMIN]"
    const char* GameDataPath = nullptr;  ///< Override signatures.jsonc; null uses the shipped one
    Core::ILogger* Logger = nullptr;     ///< Custom logger; null uses the console logger
};

/**
 * @brief Every framework service, owned as one unit for one Load/Unload cycle.
 *
 * One flat container, not a tree of per-layer ones: the layering that matters is between
 * source modules, and mirroring it in the runtime only meant plugins spelled out which
 * module owned a service (`Sdk.Messages`) instead of naming the service (`Messages`).
 * Services move between modules; the names here should not move with them.
 *
 * Declaration order is construction order and reverse destruction order, so the three
 * inter-member dependencies below (everything taking @ref Slots) are satisfied by position.
 * Plugins receive this by reference in OnLoad; framework modules reach it via @ref Detail::Rt.
 */
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * Resolve the SDK interfaces, load gamedata and bring up every subsystem, recording each
     * as a @ref LoadReport stage. False means the load must abort; @p context.Error carries why.
     */
    bool Start(const LoadContext& context);

    /** Drive the scheduler. Called once per frame from the GameFrame hook. */
    void OnGameFrame();

    /** Drop per-slot state across the services that hold any. */
    void OnPlayerDisconnect(int slot);

    // --- Core: primitives every service may reach ---
    /** Plugin-supplied permission, targeting and reply rules. Set once in OnLoad. */
    Core::PluginPolicy Policy;
    /** Named, timed load stages recorded by Start and by the plugin's OnLoad. */
    Core::LoadReport LoadReport;
    /** Frame pump, timers and delayed work. */
    Core::Scheduler Scheduler;
    /** "This slot changed hands", raised by the roster and consumed by per-slot caches. */
    Core::SlotEvents Slots;
    /** Map the server is running, captured from StartupServer. Empty after a late load
     *  until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;

    /** Translated player-facing text. */
    Core::Translations Translations;

    // --- Sdk: everything engine-facing ---
    /** Plain interface-pointer holder; populated by Start. */
    Sdk::GameInterfaces Interfaces;
    Sdk::GameData GameData;
    Sdk::MessageSystem Messages;
    Sdk::EntitySystem Entities;
    Sdk::EntityOpsService EntityOps;
    Sdk::TransmitFilterService Transmit;
    Sdk::PrecacheService Precache;
    Sdk::ConVarService ConVars;
    /** Dormant until a plugin calls Install(); removes its vtable hook on destruction. */
    Sdk::MovementHook MovementHook;
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars). */
    Sdk::NetChannelService NetChannels;
    Sdk::GameEventService Events;
    Sdk::ChatInputCapture ChatInput;
    /** Dormant until Enable(depth); listens on the MovementHook cmd feed + slot changes. */
    Sdk::InputHistoryService InputHistory{Slots};
    /** Dormant until Enable(); per-pawn Teleport hook re-bound on PlayerSpawn. */
    Sdk::TeleportTracker Teleports{Slots};
    /** Async client-side convar reads. Inert when its load stage degraded (Available() == false). */
    Sdk::ClientCvarService ClientCvars;

    // --- App: composition-root services ---
    /** Interfaces offered to, and borrowed from, other plugins. */
    App::ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig. */
    App::PluginIdentity Identity;
    /** Status sections for diagnostics commands; framework sections registered by Start. */
    App::StatusService Status;
    Menu::MenuManager Menus;

    /** Internal schema-offset service (forward-declared type). */
    Sdk::SchemaService& Schema() { return *_schema; }

    // Declared last: each name shadows its own namespace for the rest of the class body.
    Players::PlayerManager Players{Slots};
    Commands::CommandManager Commands;
    /** Completions dispatch on the game thread from the scheduler pump; the dtor stops it. */
    Http::HttpClient Http;

private:
    std::unique_ptr<Sdk::SchemaService> _schema;
    /** Declared after Scheduler so these unregister while it is still alive. */
    std::vector<Core::Subscription> _frameTimers;
};

}  // namespace VoltMod
