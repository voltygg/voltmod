#pragma once

// The one place VoltMod forward-declares anything.
//
// Every other header includes the header that defines what it names, so
// `grep -E '^(class|struct) \w+;' include/` has exactly one hit list: this file.
// Three kinds of declaration earn a place here.
//
// 1. Engine and Metamod types VoltMod only ever passes by pointer or reference.
//    Naming them costs a forward declaration; including the SDK header costs
//    every consumer a few thousand lines of tier0/entity2 and a compile-order
//    dependency on the HL2SDK.
// 2. Framework types defined under src/ and never exposed whole. A public
//    header can name them but has no header to include.
// 3. Framework types whose owning header cannot be included without an include
//    cycle, because the owner holds the namer by value. Each one says which
//    pair it belongs to.

// --- (1) Engine and Metamod ---------------------------------------------------

class CCheckTransmitInfo;
class CCommand;
class CEntityIdentity;
class CEntityInstance;
class CEntityKeyValues;
class CGameEntitySystem;
class CGlobalVars;
class CNetMessage;
class Color;
class CPlayerSlot;
class ICvar;
class IGameEvent;
class IGameEventManager2;
class IGameEventSystem;
class IGameResourceService;
class INetChannelInfo;
class INetworkMessageInternal;
class INetworkMessages;
class INetworkServerService;
class IRecipientFilter;
class ISchemaSystem;
class ISource2GameClients;  // the SDK's IServerGameClients typedef
class ISource2GameEntities;
class ISource2Server;  // the SDK's IServerGameDLL typedef
class ISource2WorldSession;
class IVEngineServer2;
class QAngle;
class Vector;

namespace SourceMM
{
class ISmmAPI;
}

namespace VoltMod
{

// --- (2) Framework internals, defined under src/ ------------------------------

/** Schema offset resolution. Defined in src/Entities/Schema.hpp. */
class SchemaService;
/** Manifest-time precache hook. Defined in src/Engine/GameSystem.hpp. */
class PrecacheGameSystem;
/** Stand-in for the SDK game-system factory. Defined in src/Engine/GameSystem.hpp. */
class GameSystemFactory;

// --- (3) Mutually recursive with their owning header --------------------------

/** Runtime.hpp holds CommandManager and MenuManager by value, so neither of
 *  those headers can include it. Phases 6-8 replace both `Runtime&` members
 *  with the narrow services they use, which retires this line. */
class Runtime;

/** MenuManager holds PlayerMenuState (Menu/Menu.hpp) by value and Menu.hpp
 *  holds MenuOption, so MenuOption.hpp cannot include MenuManager.hpp. */
class MenuManager;

/** Entity.hpp returns PlayerController by value from EntitySystem::Controller,
 *  and PlayerController.hpp needs EntitySystem, so only one side can include
 *  the other: PlayerController.hpp includes Entity.hpp. */
class PlayerController;

}  // namespace VoltMod
