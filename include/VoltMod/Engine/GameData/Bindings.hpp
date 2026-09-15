#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/GameData.hpp>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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

/**
 * Typed virtual function: its slot and the class vtable the slot is counted in. The first parameter
 * is the object it runs on, so a hook handler cannot be handed the wrong kind of object. Calling an
 * unbound function is undefined.
 */
template <class Sig>
class VirtualFn;

template <class Ret, class Object, class... Args>
class VirtualFn<Ret(Object*, Args...)>
{
public:
    VirtualFn() = default;
    VirtualFn(int index, void* table) noexcept : _index(index), _table(table) {}

    /** Bound once the slot is known to hold code in a located class table. */
    explicit operator bool() const noexcept { return _index >= 0 && _table != nullptr; }
    int Index() const noexcept { return _index; }
    void* Table() const noexcept { return _table; }

    /** Dispatch through @p object's own vtable. */
    Ret operator()(Object* object, Args... args) const
    {
        auto* vtable = *reinterpret_cast<void***>(object);
        return std::bit_cast<Ret (*)(Object*, Args...)>(vtable[_index])(object, std::forward<Args>(args)...);
    }

private:
    int _index = -1;
    void* _table = nullptr;
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
    /** Resolve all members, naming each one that does not bind in @ref Failures. NotReady when
     *  @p data is empty; otherwise an error when anything failed. */
    Status Bind(const GameData& data);

    /** `key: reason` for every member the last @ref Bind left empty. */
    std::vector<std::string> Failures;

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

    /** @defgroup CustomHudSetters CCSCustomHudLayout setters called for a @ref Screen.
     *  `self` is the entity. The real ABI uses `const CUtlString*`, never `const char*`.
     *  @ref ScreenManager::Available needs all five. @{ */
    Fn<void(void*, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClass;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClassForPlayer;
    Fn<void(void*, const CUtlString*, const CUtlString*, const CUtlString*)> CustomHudSetDialogVariable;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, const CUtlString*)>
        CustomHudSetDialogVariableForPlayer;
    Fn<void(void*, int32_t, bool)> CustomHudSetInputCapture;
    /** @} */

    /** CServerSideClient::FilterMessage(const CNetMessage*, INetChannel*). Hooked for @ref ScreenManager::Pressed. */
    Fn<bool(EngineMessageFilter*, const CNetMessage*, void*)> FilterMessage;
    /** CNetworkGameServer::ReplyConnection(CServerSideClient*), which names the addons a client
     *  mounts. Hooked by @ref Addons. */
    Fn<void(EngineServer*, EngineClient*)> ReplyConnection;

    /** IGameEventManager2** inside CSource2Server. */
    Address GameEventManager;
    /** CBaseGameSystemFactory** list head. */
    Address GameSystemFactoryList;
    /** CGameSystemEventDispatcher** used to detach systems on unload. */
    Address GameSystemEventDispatcher;
    /** CUtlVector<AddedGameSystem_t>* used to remove systems on unload. */
    Address GameSystemList;

    /** CBasePlayerPawn::CommitSuicide(bool explode, bool force), counted in CCSPlayerPawn. */
    VirtualFn<void(CEntityInstance*, bool, bool)> CommitSuicide;
    /** CCSPlayerController::ChangeTeam(int team). */
    VirtualFn<void(CEntityInstance*, int)> ChangeTeam;
    /** CCSPlayerController::Respawn(). */
    VirtualFn<void(CEntityInstance*)> Respawn;
    /** CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*), hooked on CCSPlayerPawn. */
    VirtualFn<void(CEntityInstance*, const Vector*, const QAngle*, const Vector*)> Teleport;
    /** CPlayer_MovementServices::RunCommand(CUserCmd*), hooked on CCSPlayer_MovementServices. */
    VirtualFn<void*(EngineMovementServices*, void*)> RunCommand;
    /** CCSPlayer_ItemServices::GiveNamedItem(const char* classname). */
    VirtualFn<void*(void*, const char*)> GiveNamedItem;
    /** CCSPlayer_ItemServices::RemoveAllItems(bool removeSuit). */
    VirtualFn<void(void*, bool)> RemoveAllItems;
    /** CServerSideClient::ProcessRespondCvarValue(...), hooked on CServerSideClient. */
    VirtualFn<bool(EngineClient*, const void*)> ProcessRespondCvarValue;
    /** CServerSideClient::SendNetMessage(const CNetMessage*, NetChannelBufType_t), hooked on
     *  CServerSideClient. The SDK enum is represented as int here. */
    VirtualFn<bool(EngineClient*, const CNetMessage*, int)> SendNetMessage;

    /** CGameEntitySystem* cached inside IGameResourceService. */
    OffsetOf<CGameEntitySystem*> GameEntitySystem;
    /** Recipient player slot inside CCheckTransmitInfo. */
    OffsetOf<uint8_t> VisibilityRecipientSlot;
    /** Player slot inside CServerSideClient. */
    OffsetOf<int> ClientSlot;
    /** SteamID inside CServerSideClient. Unaligned; read through memcpy. */
    OffsetOf<int64_t> ClientSteamId;
    /** Bytes from CServerSideClient to the base FilterMessage runs on. */
    OffsetOf<void> ClientMessageFilter;
    /** CNetworkGameServer::m_szAddons (CUtlString), copied into each connection reply. */
    OffsetOf<void> ServerAddons;
    /** CSGOUserCmdPB payload embedded in CUserCmd. */
    OffsetOf<void> UserCmdProto;
    /** CUserCmd command counter; live clients leave the protobuf counter at zero. */
    OffsetOf<int32_t> UserCmdNumber;
};

}  // namespace VoltMod
