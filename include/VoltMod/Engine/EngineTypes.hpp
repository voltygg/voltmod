#pragma once

// VoltMod's only forward declarations, for three reasons:
// 1. SDK types passed by pointer or reference, so consumers never include the SDK.
// 2. Framework types defined under src/, which have no public header.
// 3. One side of a pair of headers that would otherwise include each other.

#include <cstdint>

// 1. SDK and Metamod types.
class CCheckTransmitInfo;
class CCommand;
class CEntityIdentity;
class CEntityInstance;
class CEntityKeyValues;
class CGameEntitySystem;
class CGameTrace;
class CGlobalVars;
class CNetMessage;
class CNetworkGameServerBase;
class CSchemaSystemTypeScope;
class CTraceFilter;
class CUtlString;  // the custom HUD setters take `const CUtlString*`
class CPlayerSlot;
class ICvar;
class IEntityResourceManifest;
class IGameEvent;
class IGameEventManager2;
class IGameEventSystem;
class IGameResourceService;
class INetChannel;
class INetChannelInfo;
class INetworkMessageInternal;
class INetworkMessageProcessingPreFilter;
class INetworkMessages;
class INetworkServerService;
class IRecipientFilter;
class ISchemaSystem;
class ISource2GameClients;  // IServerGameClients
class ISource2GameEntities;
class ISource2Server;  // IServerGameDLL
class ISource2WorldSession;
class IVEngineServer2;
class QAngle;
struct Ray_t;
class Vector;
union CVValue_t;
enum EConVarType : int16_t;
enum NetChannelBufType_t : int8_t;

namespace SourceMM
{
class ISmmAPI;
}

namespace KHook
{
/** Metamod's hook dispatcher, handed to each plugin through @ref VoltMod::IHost. */
class IKHook;
}  // namespace KHook

namespace VoltMod
{

/** Tags for engine classes the SDK does not declare; only ever pointed to. */
class EnginePawn  // CCSPlayerPawn and the pawns sharing its vtable
{};
class EngineClient  // CServerSideClient
{};
class EngineMovementServices  // CCSPlayer_MovementServices
{};
class EngineNavPhysics  // the nav mesh's physics interface, used by @ref Trace
{};

// 2. Defined under src/.
class ScreenEntity;          // Ui/ScreenEntity.hpp
class ButtonPressHook;       // Ui/ButtonPressHook.hpp
class PendingConVarQueries;  // Hooks/PendingConVarQueries.hpp
class AddonDownloads;        // Workshop/AddonDownloads.hpp
class CommandRouter;         // Commands/CommandRouter.hpp
class EngineArgBinder;       // Commands/ArgBinding.hpp

// 3. Include cycles.
class EntitySystem;  // Entity.hpp points to it; EntitySystem.hpp returns entities by value
class Controller;    // Pawn.hpp returns it; Controller.hpp includes Pawn.hpp
class Pawn;          // Player.hpp returns both without pulling the schema headers into SDK-free tests
class GlowVision;    // Visibility.hpp returns it; GlowVision.hpp includes Visibility.hpp

}  // namespace VoltMod
