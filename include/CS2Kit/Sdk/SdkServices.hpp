#pragma once

#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/SlotEvents.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <CS2Kit/Sdk/ClientCvarService.hpp>
#include <CS2Kit/Sdk/ConVarService.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/EntityOps.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/InputHistoryService.hpp>
#include <CS2Kit/Sdk/MovementHook.hpp>
#include <CS2Kit/Sdk/NetChannel.hpp>
#include <CS2Kit/Sdk/PrecacheService.hpp>
#include <CS2Kit/Sdk/TeleportTracker.hpp>
#include <CS2Kit/Sdk/TransmitFilter.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <memory>

namespace CS2Kit::Sdk
{
class SchemaService;  // internal (src/Sdk/Schema.hpp) - by pointer, so this header stays clean

/**
 * @brief Every engine-facing service, owned as one unit.
 *
 * Sdk is the bottom of the service stack, so this aggregate holds its own members by
 * value and takes references to the two things it needs from below - the Core scheduler
 * and the Utils translation table. It never reaches upward: nothing here knows about
 * players, commands or menus.
 *
 * Reached from inside the Sdk module via @ref Ctx(); plugins go through `Engine()`,
 * which forwards to these same objects.
 */
class SdkServices
{
public:
    SdkServices(Core::Scheduler& scheduler, Core::SlotEvents& slots, Utils::Translations& translations);
    ~SdkServices();
    SdkServices(const SdkServices&) = delete;
    SdkServices& operator=(const SdkServices&) = delete;

    // From below. Held by reference: the composition root owns them and outlives this.
    Core::Scheduler& Scheduler;
    Utils::Translations& Translations;

    // Declaration order == construction order; destruction is the reverse.
    GameInterfaces Interfaces;  // plain interface-pointer holder; populated in CS2Kit::Initialize
    GameData GameData;
    MessageSystem Messages;
    EntitySystem Entities;
    EntityOpsService EntityOps;
    TransmitFilterService Transmit;
    PrecacheService Precache;
    ConVarService ConVars;
    /** Dormant until a plugin calls Install(); removes its vtable hook on destruction. */
    MovementHook MovementHook;
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars). */
    NetChannelService NetChannels;
    GameEventService Events;
    ChatInputCapture ChatInput;
    /** Dormant until Enable(depth); listens on the MovementHook cmd feed + slot changes. */
    InputHistoryService InputHistory;
    /** Dormant until Enable(); per-pawn Teleport hook re-bound on PlayerSpawn. */
    TeleportTracker Teleports;
    /** Async client-side convar reads. Inert when its load stage degraded (Available() == false). */
    ClientCvarService ClientCvars;

    /** Internal schema-offset service (forward-declared type). */
    SchemaService& Schema() { return *_schema; }

private:
    std::unique_ptr<SchemaService> _schema;  // constructed last, before public members are used
};

/** Set/clear the active SdkServices. Called by the composition root on Load/Unload. */
void SetActiveSdkServices(SdkServices* services);

/** The active SdkServices. Asserts if called outside a Load/Unload window. */
SdkServices& Ctx();

/** The active SdkServices, or nullptr - for teardown paths that may run after Shutdown. */
SdkServices* CtxOrNull();

}  // namespace CS2Kit::Sdk
