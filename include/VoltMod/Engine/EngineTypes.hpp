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
// tier1's ref-counted string. Named only as `const CUtlString*` in the CustomUi setter
// prototypes, which is the real ABI of those functions rather than a convenience.
class CUtlString;
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

/** Manifest-time precache hook. Defined in src/Engine/GameSystem.hpp. */
class PrecacheGameSystem;
/** Stand-in for the SDK game-system factory. Defined in src/Engine/GameSystem.hpp. */
class GameSystemFactory;

/** Everything a UiPanel keeps between calls: the entity, the write cache and the click routing.
 *  Held by shared_ptr so a move does not move the events handlers point at. Defined in
 *  src/Ui/UiPanelState.hpp. */
struct UiPanelState;
/** The FilterMessage hook behind CustomUi::Clicked. Held by unique_ptr so no public header
 *  reaches VtableHook.hpp. Defined in src/Ui/UiClicks.hpp. */
class UiClicks;
/** The row driver behind UiMenuManager. Held by unique_ptr because the menu layout's row contract
 *  is the framework's, not a consumer's. Defined in src/Menu/Ui/UiList.hpp. */
class UiList;

// --- (3) Mutually recursive with their owning header --------------------------

/** MenuHost holds PlayerMenuState (Menu/Menu.hpp) by value and Menu.hpp
 *  holds MenuOption, so MenuOption.hpp cannot include MenuHost.hpp. */
class MenuHost;

/** Entity.hpp holds an EntitySystem* so a wrapper's verbs can reach Bindings and
 *  the entity system, while EntitySystem.hpp returns Entity, Pawn and Controller
 *  by value. Only one side can include the other: EntitySystem.hpp includes
 *  Controller.hpp, which includes Pawn.hpp, which includes Entity.hpp. */
class EntitySystem;

/** Pawn.hpp returns Controller by value from Pawn::GetController and
 *  Controller.hpp includes Pawn.hpp, so this is the same pair as above one level
 *  down.
 *
 *  Player.hpp declares `Controller Ctrl()` and `Pawn GetPawn()` from these two as
 *  well. A return type in a declaration may be incomplete, and including the
 *  wrappers instead would put a `Field<Vector, ...>` - whose default ExpectedSize
 *  is `sizeof(Vector)` - in every translation unit that names a Player, which
 *  means the SDK's mathlib in the SDK-free unit tests. */
class Controller;
class Pawn;

}  // namespace VoltMod
