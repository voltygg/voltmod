#pragma once

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/GameData.hpp>
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

/** Opaque address for an ABI declared only in its implementation file. */
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

/** Typed virtual function. Its signature omits the instance passed to @ref Call. */
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

/** Primary class vtable and its class name for DVP hooks. */
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

/** DVP hook slot and class table resolved from one gamedata entry. @p Object names which engine
 *  class the slot dispatches on, so a handler cannot be handed the wrong kind of object. */
template <class Object, class Sig>
struct VHookBinding
{
    VFn<Sig> Method;
    VTableRef Table;

    explicit operator bool() const noexcept { return static_cast<bool>(Method) && static_cast<bool>(Table); }
};

/** Typed byte offset. Unbound access is inert and reads/writes support unaligned fields. */
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

/** Byte offset for an embedded type declared only in an implementation file. */
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

    /** ABI: CBaseEntity* (const char* className, int forceEdictIndex). */
    Fn<CEntityInstance*(const char*, int)> CreateEntityByName;
    /** ABI: void (CBaseEntity*, CEntityKeyValues*); keyvalues may be null. */
    Fn<void(CEntityInstance*, CEntityKeyValues*)> DispatchSpawn;
    /** ABI: void (CEntityInstance*, const char* input, activator, caller, variant_t*, int outputId, void*). */
    Fn<void(CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, int, void*)> AcceptInput;
    /** ABI: void (CEntitySystem*, target, input, activator, caller, variant_t*, float delay, int outputId, void*, void*). */
    Fn<void(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, float, int, void*, void*)>
        AddEntityIOEvent;
    /** ABI: void (CEntityInstance*). */
    Fn<void(CEntityInstance*)> UtilRemove;
    /** ABI: void (CBaseModelEntity*, const char* modelPath). */
    Fn<void(CEntityInstance*, const char*)> SetModel;
    /** ABI: void (CBaseEntity*, const char* soundEvent, int pitch, float volume, float delay). */
    Fn<void(CEntityInstance*, const char*, int, float, float)> EmitSoundParams;
    /** ABI: StartSoundEventInfo (IRecipientFilter&, CEntityIndex, const EmitSound_t&), defined in EntityOps.cpp. */
    Address EmitSoundFilter;
    /** ABI: CBaseEntity* (CEntitySystem*, CEntityInstance* startAfter, const char* className). */
    Fn<CEntityInstance*(void*, CEntityInstance*, const char*)> FindEntityByClassName;
    /** ABI: CBaseEntity* (CEntitySystem*, startAfter, name, searching, activator, caller, IEntityFindFilter*). */
    Fn<CEntityInstance*(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, CEntityInstance*,
                        void*)>
        FindEntityByName;
    /** ABI: IGameEventListener2* (CPlayerSlot), defined in GameEvents.cpp. */
    Address LegacyGameEventListener;

    /** @defgroup CustomHudSetters CCSCustomHudLayout setters called by @ref UiPanels.
     *  `self` is the entity. The real ABI uses `const CUtlString*`, never `const char*`. All five
     *  bind together or not at all; @ref Capability::CustomUi reports failure. @{ */
    Fn<void(void*, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClass;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClassForPlayer;
    Fn<void(void*, const CUtlString*, const CUtlString*, const CUtlString*)> CustomHudSetDialogVariable;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, const CUtlString*)>
        CustomHudSetDialogVariableForPlayer;
    Fn<void(void*, int32_t, bool)> CustomHudSetInputCapture;
    /** @} */

    /** CServerSideClient::FilterMessage. It is in a secondary vtable, so @ref UiClickHook finds
     *  its slot from this signature with FindVTableSlot instead of using an index. */
    Address FilterMessage;

    /** IGameEventManager2** inside CSource2Server. */
    Address GameEventManager;
    /** CBaseGameSystemFactory** list head. */
    Address GameSystemFactoryList;
    /** CGameSystemEventDispatcher** used to detach systems on unload. */
    Address GameSystemEventDispatcher;
    /** CUtlVector<AddedGameSystem_t>* used to remove systems on unload. */
    Address GameSystemList;

    /** CBasePlayerPawn::CommitSuicide(bool explode, bool force). */
    VFn<void(bool, bool)> CommitSuicide;
    /** CCSPlayerController::ChangeTeam(int team). */
    VFn<void(int)> ChangeTeam;
    /** CCSPlayerController::Respawn(). */
    VFn<void()> Respawn;
    /** CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*), hooked on CCSPlayerPawn.
     *  A direct call only needs Method; the table is what the hook adds. */
    VHookBinding<HookedPawn, void(const Vector*, const QAngle*, const Vector*)> Teleport;
    /** CPlayer_MovementServices::RunCommand(CUserCmd*), hooked on CCSPlayer_MovementServices. */
    VHookBinding<HookedMovementServices, void*(void*)> RunCommand;
    /** CCSPlayer_ItemServices::GiveNamedItem(const char* classname). */
    VFn<void*(const char*)> GiveNamedItem;
    /** CCSPlayer_ItemServices::RemoveAllItems(bool removeSuit). */
    VFn<void(bool)> RemoveAllItems;
    /** CServerSideClient::ProcessRespondCvarValue(...), hooked on CServerSideClient. */
    VHookBinding<HookedClient, bool(const void*)> ProcessRespondCvarValue;
    /** CServerSideClient::SendNetMessage(const CNetMessage*, NetChannelBufType_t), hooked on
     *  CServerSideClient. The SDK enum is represented as int here. */
    VHookBinding<HookedClient, bool(const CNetMessage*, int)> SendNetMessage;

    /** CGameEntitySystem* cached inside IGameResourceService. */
    OffsetOf<CGameEntitySystem*> GameEntitySystem;
    /** Recipient player slot inside CCheckTransmitInfo. */
    OffsetOf<uint8_t> CheckTransmitPlayerSlot;
    /** Player slot inside CServerSideClient. */
    OffsetOf<int> ServerSideClientSlot;
    /** CNetworkGameServer::m_Clients, the slot-indexed client vector. See ServerSideClients.hpp. */
    OffsetOf<void> NetworkGameServerClients;
    /** SteamID inside CServerSideClient. Unaligned; read through memcpy. */
    OffsetOf<int64_t> ServerSideClientSteamId;
    /** CSGOUserCmdPB payload embedded in CUserCmd. */
    OffsetOf<void> UserCmdPB;
    /** CUserCmd command counter; live clients leave the protobuf counter at zero. */
    OffsetOf<int32_t> UserCmdNumber;
};

}  // namespace VoltMod
