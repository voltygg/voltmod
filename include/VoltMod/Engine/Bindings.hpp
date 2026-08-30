#pragma once

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @file Bindings.hpp
 * @brief Typed engine ABIs resolved from gamedata locations.
 */

/** Address for an ABI that can only be named in its implementation file. */
class Address
{
public:
    Address() = default;
    explicit Address(void* address) noexcept : _address(address) {}

    explicit operator bool() const noexcept { return _address != nullptr; }
    void* Ptr() const noexcept { return _address; }

private:
    void* _address = nullptr;
};

/** Typed engine function. Calling an unbound function is undefined. */
template <class Sig>
class Fn;

template <class Ret, class... Args>
class Fn<Ret(Args...)>
{
public:
    Fn() = default;
    explicit Fn(void* address) noexcept : _address(address) {}

    explicit operator bool() const noexcept { return _address != nullptr; }
    void* Ptr() const noexcept { return _address; }

    Ret operator()(Args... args) const
    {
        return std::bit_cast<Ret (*)(Args...)>(_address)(std::forward<Args>(args)...);
    }

private:
    void* _address = nullptr;
};

/** Typed virtual function. The signature excludes the explicit instance passed to @ref Call. */
template <class Sig>
class VFn;

template <class Ret, class... Args>
class VFn<Ret(Args...)>
{
public:
    VFn() = default;
    explicit constexpr VFn(int index) noexcept : _index(index) {}

    explicit constexpr operator bool() const noexcept { return _index >= 0; }

    /** Vtable slot used by hook setup. */
    constexpr int Index() const noexcept { return _index; }

    /** Dispatch on @p instance. Both the binding and instance must be valid. */
    Ret Call(void* instance, Args... args) const
    {
        auto* vtable = *reinterpret_cast<void***>(instance);
        return std::bit_cast<Ret (*)(void*, Args...)>(vtable[_index])(instance, std::forward<Args>(args)...);
    }

private:
    int _index = -1;
};

/** Primary class vtable and its diagnostic name for DVP hooks. */
class VTableRef
{
public:
    VTableRef() = default;
    VTableRef(std::string className, void* table) : _class(std::move(className)), _table(table) {}

    explicit operator bool() const noexcept { return _table != nullptr; }
    void* Table() const noexcept { return _table; }
    std::string_view Class() const noexcept { return _class; }

private:
    std::string _class;
    void* _table = nullptr;
};

/** DVP hook slot and class table from one gamedata entry. */
template <class Sig>
struct VHookBinding
{
    VFn<Sig> Method;
    VTableRef Table;

    explicit operator bool() const noexcept { return static_cast<bool>(Method) && static_cast<bool>(Table); }
};

/** Typed byte offset. Unbound access is inert; `memcpy` permits unaligned access. */
template <class T>
class OffsetOf
{
public:
    OffsetOf() = default;
    explicit constexpr OffsetOf(int value) noexcept : _value(value) {}

    explicit constexpr operator bool() const noexcept { return _value >= 0; }
    constexpr int Value() const noexcept { return _value; }

    T Read(const void* base) const
    {
        T out{};
        if (_value >= 0 && base)
            std::memcpy(&out, static_cast<const uint8_t*>(base) + _value, sizeof(T));
        return out;
    }

    void Write(void* base, const T& value) const
    {
        if (_value >= 0 && base)
            std::memcpy(static_cast<uint8_t*>(base) + _value, &value, sizeof(T));
    }

private:
    int _value = -1;
};

/** Byte offset for an embedded type named only by its implementation file. */
template <>
class OffsetOf<void>
{
public:
    OffsetOf() = default;
    explicit constexpr OffsetOf(int value) noexcept : _value(value) {}

    explicit constexpr operator bool() const noexcept { return _value >= 0; }
    constexpr int Value() const noexcept { return _value; }

    const void* Ptr(const void* base) const
    {
        if (_value < 0 || !base)
            return nullptr;
        return static_cast<const uint8_t*>(base) + _value;
    }

private:
    int _value = -1;
};

/** Typed gamedata bindings shared by engine-facing services. */
struct Bindings
{
    /** Resolve all members. Returns NotReady when @p data is empty. */
    Status Bind(const GameData& data, Capabilities& caps);

    // Signatures

    /** CBaseEntity* (const char* className, int forceEdictIndex) */
    Fn<CEntityInstance*(const char*, int)> CreateEntityByName;
    /** void (CBaseEntity*, CEntityKeyValues*), with nullable keyvalues. */
    Fn<void(CEntityInstance*, CEntityKeyValues*)> DispatchSpawn;
    /** void (CEntityInstance*, const char* input, activator, caller, variant_t*, int outputId, void*) */
    Fn<void(CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, int, void*)> AcceptInput;
    /** void (CEntitySystem*, target, input, activator, caller, variant_t*, float delay, int outputId, void*, void*) */
    Fn<void(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, float, int, void*, void*)>
        AddEntityIOEvent;
    /** void (CEntityInstance*) */
    Fn<void(CEntityInstance*)> UtilRemove;
    /** void (CBaseModelEntity*, const char* modelPath) */
    Fn<void(CEntityInstance*, const char*)> SetModel;
    /** void (CBaseEntity*, const char* soundEvent, int pitch, float volume, float delay) */
    Fn<void(CEntityInstance*, const char*, int, float, float)> EmitSoundParams;
    /** StartSoundEventInfo (IRecipientFilter&, CEntityIndex, const EmitSound_t&), defined in EntityOps.cpp. */
    Address EmitSoundFilter;
    /** CBaseEntity* (CEntitySystem*, CEntityInstance* startAfter, const char* className) */
    Fn<CEntityInstance*(void*, CEntityInstance*, const char*)> FindEntityByClassName;
    /** CBaseEntity* (CEntitySystem*, startAfter, name, searching, activator, caller, IEntityFindFilter*) */
    Fn<CEntityInstance*(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, CEntityInstance*,
                        void*)>
        FindEntityByName;
    /** IGameEventListener2* (CPlayerSlot), defined in GameEvents.cpp. */
    Address LegacyGameEventListener;

    /** @defgroup CustomHudSetters CCSCustomHudLayout setters, called by @ref CustomUi.
     *  `self` is the entity. The `const CUtlString*` parameters are the real ABI; strings are
     *  never passed as `const char*` here. All five bind together or none does - a half-bound set
     *  would let a call through a null address - which @ref Capability::CustomUi reports.
     *  @{ */
    Fn<void(void*, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClass;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClassForPlayer;
    Fn<void(void*, const CUtlString*, const CUtlString*, const CUtlString*)> CustomHudSetDialogVariable;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, const CUtlString*)>
        CustomHudSetDialogVariableForPlayer;
    Fn<void(void*, int32_t, bool)> CustomHudSetInputCapture;
    /** @} */

    /** CServerSideClient::FilterMessage, bound by signature rather than vtable index because it
     *  lives in a secondary vtable. @ref UiClicks turns this address into a hookable slot
     *  with FindVTableSlot; see the gamedata comment for why there is no index. */
    Address FilterMessage;

    /** The user-message type a custom HUD Button press arrives as. A bare number from gamedata:
     *  nothing in the engine's registry or the SDK's protos names this message. -1 when unbound. */
    int32_t CustomHudClicked = -1;

    // Addresses

    /** IGameEventManager2** inside CSource2Server. */
    Address GameEventManager;
    /** CBaseGameSystemFactory** list head. */
    Address GameSystemFactoryList;
    /** CGameSystemEventDispatcher** used to detach on unload. */
    Address GameSystemEventDispatcher;
    /** CUtlVector<AddedGameSystem_t>* used to remove systems on unload. */
    Address GameSystemList;

    // Virtual functions

    /** CBasePlayerPawn::CommitSuicide(bool explode, bool force) */
    VFn<void(bool, bool)> CommitSuicide;
    /** CCSPlayerController::ChangeTeam(int team) */
    VFn<void(int)> ChangeTeam;
    /** CCSPlayerController::Respawn() */
    VFn<void()> Respawn;
    /** CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*) */
    VFn<void(const Vector*, const QAngle*, const Vector*)> Teleport;
    /** CPlayer_MovementServices::RunCommand(CUserCmd*), hooked on CCSPlayer_MovementServices. */
    VHookBinding<void*(void*)> RunCommand;
    /** CCSPlayer_ItemServices::GiveNamedItem(const char* classname) */
    VFn<void*(const char*)> GiveNamedItem;
    /** CCSPlayer_ItemServices::RemoveAllItems(bool removeSuit) */
    VFn<void(bool)> RemoveAllItems;
    /** CServerSideClient::ProcessRespondCvarValue(...), hooked on CServerSideClient. */
    VHookBinding<bool(const void*)> ProcessRespondCvarValue;
    /** CServerSideClient::SendNetMessage(const CNetMessage*, NetChannelBufType_t), hooked on
     *  CServerSideClient. The buf type is an enum the SDK declares, so it is taken as int here. */
    VHookBinding<bool(const void*, int)> SendNetMessage;

    // Offsets

    /** The CGameEntitySystem* cached inside IGameResourceService. */
    OffsetOf<CGameEntitySystem*> GameEntitySystem;
    /** The recipient player slot inside CCheckTransmitInfo. */
    OffsetOf<uint8_t> CheckTransmitPlayerSlot;
    /** The player slot inside CServerSideClient. */
    OffsetOf<int> ServerSideClientSlot;
    /** CNetworkGameServer::m_Clients, the slot-indexed client vector. See ServerSideClients.hpp. */
    OffsetOf<void> NetworkGameServerClients;
    /** The SteamID inside CServerSideClient. Unaligned; read through memcpy. */
    OffsetOf<int64_t> ServerSideClientSteamId;
    /** The CSGOUserCmdPB payload embedded in CUserCmd. */
    OffsetOf<void> UserCmdPB;
    /** CUserCmd command counter; live clients leave the protobuf counter at zero. */
    OffsetOf<int32_t> UserCmdNumber;
};

}  // namespace VoltMod
