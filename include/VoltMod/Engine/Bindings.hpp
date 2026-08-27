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
 * @brief The typed view of gamedata: C++ owns every ABI, the JSON owns every location.
 *
 * Each member below is one gamedata key, and its C++ type is the contract - the prototype, the
 * vtable signature, the field type. Services take `const Bindings&` and read a field; nothing
 * looks an entry up by string on a call path, so a renamed or missing key is a compile error or a
 * load-time capability failure, never a silent null at the moment it is used.
 */

/**
 * @brief A resolved absolute address whose real prototype a public header cannot name.
 *
 * Used only where the ABI involves a type defined in one .cpp - a struct returned by value, a
 * template instantiation. That translation unit keeps the prototype and casts @ref Ptr itself.
 */
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

/**
 * @brief A resolved engine function, called through its declared prototype.
 *
 * `explicit operator bool` is false until it binds; calling an unbound @ref Fn is undefined, so
 * every caller checks (or checks the owning @ref Capability) first.
 */
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
 * @brief A virtual function, by the vtable index gamedata gives it.
 *
 * The parameter list is the callee's, without the implicit `this`: @ref Call takes the instance
 * separately. The game's vfuncs take their parameters by value, and spelling the signature out
 * here is what keeps an lvalue argument from being passed by address.
 */
template <class Sig>
class VFn;

template <class Ret, class... Args>
class VFn<Ret(Args...)>
{
public:
    VFn() = default;
    explicit constexpr VFn(int index) noexcept : _index(index) {}

    explicit constexpr operator bool() const noexcept { return _index >= 0; }

    /** The raw slot, for the hook macros that reconfigure a manual hook rather than call it. */
    constexpr int Index() const noexcept { return _index; }

    /** Dispatch on @p instance. Undefined when this is unbound or @p instance is null. */
    Ret Call(void* instance, Args... args) const
    {
        auto* vtable = *reinterpret_cast<void***>(instance);
        return std::bit_cast<Ret (*)(void*, Args...)>(vtable[_index])(instance, std::forward<Args>(args)...);
    }

private:
    int _index = -1;
};

/**
 * @brief A class's primary virtual function table, for hooks that bind the table itself.
 *
 * A DVP hook covers every instance of the class at once, which is what lets a hook install from
 * OnLoad with no player connected. The class name is kept for the log line, since a table found
 * under a drifted name is the failure this is most likely to hit.
 */
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

/**
 * @brief A validated byte offset to a `T` inside a layout the SDK does not declare.
 *
 * Reads and writes go through memcpy, so an offset that lands on an unaligned address is a wrong
 * value rather than undefined behaviour. Unbound reads return `T{}` and unbound writes do nothing.
 */
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

/**
 * @brief A validated byte offset to an embedded field whose type this header cannot name.
 *
 * The one case is a generated protobuf message sitting inside an engine struct: the reader casts
 * @ref Ptr to the type its own translation unit includes.
 */
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

/**
 * @brief Every gamedata entry the framework uses, typed.
 *
 * The Runtime owns one, binds it once in Start, and hands `const Bindings&` to the services that
 * need it. A member's name is its gamedata key; the comment above it is the engine prototype it
 * stands for.
 */
struct Bindings
{
    /**
     * Bind every member from @p data and record the outcome of each capability in @p caps.
     *
     * Called once by `Runtime::Start`. Each capability is set from the keys it needs, with the
     * first failing key and its reason as the text. Individual bindings that no capability gates
     * are simply left empty, and their `explicit operator bool` is what the caller checks.
     *
     * @return Error::NotReady when @p data holds nothing, i.e. the gamedata file did not load.
     */
    Status Bind(const GameData& data, Capabilities& caps);

    // --- Engine functions located by byte pattern ------------------------------------------

    /** CBaseEntity* (const char* className, int forceEdictIndex) */
    Fn<CEntityInstance*(const char*, int)> CreateEntityByName;
    /** void (CBaseEntity*, CEntityKeyValues*) - the keyvalues may be null. */
    Fn<void(CEntityInstance*, CEntityKeyValues*)> DispatchSpawn;
    /** void (CEntityInstance*, const char* input, activator, caller, variant_t*, int outputId, void*) */
    Fn<void(CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, int, void*)> AcceptInput;
    /** void (CEntitySystem*, target, input, activator, caller, variant_t*, float delay, int outputId, void*, void*) */
    Fn<void(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, float, int, void*, void*)>
        AddEntityIOEvent;
    /** void (CEntityInstance*) - immediate entity removal. */
    Fn<void(CEntityInstance*)> UtilRemove;
    /** void (CBaseModelEntity*, const char* modelPath) */
    Fn<void(CEntityInstance*, const char*)> SetModel;
    /** void (CBaseEntity*, const char* soundEvent, int pitch, float volume, float delay) */
    Fn<void(CEntityInstance*, const char*, int, float, float)> EmitSoundParams;
    /** StartSoundEventInfo (IRecipientFilter&, CEntityIndex, const EmitSound_t&) - returned by value
     *  through the hidden sret pointer, so the prototype only exists in EntityOps.cpp. */
    Address EmitSoundFilter;
    /** CBaseEntity* (CEntitySystem*, CEntityInstance* startAfter, const char* className) */
    Fn<CEntityInstance*(void*, CEntityInstance*, const char*)> FindEntityByClassName;
    /** CBaseEntity* (CEntitySystem*, startAfter, name, searching, activator, caller, IEntityFindFilter*) */
    Fn<CEntityInstance*(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, CEntityInstance*,
                        void*)>
        FindEntityByName;
    /** IGameEventListener2* (CPlayerSlot) - CPlayerSlot is passed by value, so the prototype only
     *  exists in GameEvents.cpp. */
    Address LegacyGameEventListener;

    // --- Engine data located through a rel32 displacement ----------------------------------

    /** IGameEventManager2** inside CSource2Server. */
    Address GameEventManager;
    /** CBaseGameSystemFactory** - head of the engine's game-system factory list. */
    Address GameSystemFactoryList;
    /** CGameSystemEventDispatcher** - the live dispatcher, for detach on unload. */
    Address GameSystemEventDispatcher;
    /** CUtlVector<AddedGameSystem_t>* - active game systems, for removal on unload. */
    Address GameSystemList;

    // --- Virtual functions ------------------------------------------------------------------

    /** CBasePlayerPawn::CommitSuicide(bool explode, bool force) */
    VFn<void(bool, bool)> CommitSuicide;
    /** CCSPlayerController::ChangeTeam(int team) */
    VFn<void(int)> ChangeTeam;
    /** CCSPlayerController::Respawn() */
    VFn<void()> Respawn;
    /** CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*) */
    VFn<void(const Vector*, const QAngle*, const Vector*)> Teleport;
    /** CPlayer_MovementServices::RunCommand(CUserCmd*) - hooked, never called. */
    VFn<void*(void*)> RunCommand;
    /** CCSPlayer_ItemServices::GiveNamedItem(const char* classname) */
    VFn<void*(const char*)> GiveNamedItem;
    /** CCSPlayer_ItemServices::RemoveAllItems(bool removeSuit) */
    VFn<void(bool)> RemoveAllItems;
    /** CServerSideClient::ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>&) -
     *  hooked, never called, so the message template never has to be named here. */
    VFn<bool(const void*)> ProcessRespondCvarValue;

    // --- Class vtables, for the hooks that bind the table ------------------------------------

    /** CCSPlayer_MovementServices in `server`; the RunCommand hook binds this. */
    VTableRef MovementServices;
    /** CServerSideClient in `engine2`; the client-convar response hook binds this. */
    VTableRef ServerSideClient;

    // --- Byte offsets into undeclared layouts -------------------------------------------------

    /** The CGameEntitySystem* cached inside IGameResourceService. */
    OffsetOf<CGameEntitySystem*> GameEntitySystem;
    /** The recipient player slot inside CCheckTransmitInfo. */
    OffsetOf<uint8_t> CheckTransmitPlayerSlot;
    /** The player slot inside CServerSideClient. */
    OffsetOf<int> ServerSideClientSlot;
    /** The CSGOUserCmdPB payload embedded in CUserCmd. */
    OffsetOf<void> UserCmdPB;
    /** The engine's own command counter in CUserCmd (the protobuf's is left at 0 by live clients). */
    OffsetOf<int32_t> UserCmdNumber;
};

}  // namespace VoltMod
