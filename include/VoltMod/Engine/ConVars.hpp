#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace VoltMod
{

/**
 * @brief One engine-side convar change, as @ref ConVars::Changed reports it.
 *
 * All three views borrow engine storage for the duration of the handler; copy what you keep.
 */
struct ConVarChange
{
    std::string_view Name;
    std::string_view OldValue;
    std::string_view NewValue;
};

template <class T>
class ConVar;

/**
 * @brief A raw convar flip that undoes itself.
 *
 * Holds the value the convar had when it was constructed and writes it back on destruction, so a
 * flip cannot leak out of the code that made it. The convar it refers to must outlive the scope -
 * keep the @ref ConVar in a member and the scope in a shorter-lived one.
 */
template <class T>
class ConVarRawScope
{
public:
    /** Flip @p cvar to @p value now and restore its current value on destruction. */
    ConVarRawScope(ConVar<T>& cvar, const T& value)
        requires(!std::is_same_v<T, std::string>);
    ~ConVarRawScope();

    ConVarRawScope(const ConVarRawScope&) = delete;
    ConVarRawScope& operator=(const ConVarRawScope&) = delete;
    ConVarRawScope(ConVarRawScope&& other) noexcept
        : _cvar(std::exchange(other._cvar, nullptr)), _previous(std::move(other._previous))
    {}
    ConVarRawScope& operator=(ConVarRawScope&&) = delete;

private:
    ConVar<T>* _cvar = nullptr;
    T _previous{};
};

class ConVars;

/**
 * @brief A typed handle to one engine convar.
 *
 * Resolved once by name and then read and written without another lookup. `T` is the convar's own
 * engine type - `bool`, `int`, `float`, or `std::string` - and @ref Find refuses a convar of a
 * different kind, so the silent no-op of setting an `int` on a `bool` convar cannot happen.
 *
 * A registered convar outlives map changes, so a handle can be cached for the whole load cycle.
 * A default-constructed or unresolved handle is falsy, reads as `T{}`, and refuses every write.
 *
 * @code
 * auto impulse = ConVar<float>::Find(runtime.ConVars, "sv_jump_impulse");
 * if (impulse)
 *     velocity.z = impulse->Get();
 * @endcode
 */
template <class T>
class ConVar
{
public:
    ConVar() = default;

    /**
     * Resolve @p name through @p service.
     * @return Error::NotFound when the server has no such convar (or it is not registered yet),
     *         Error::Invalid when it exists but its engine type is not @p T.
     */
    static Result<ConVar<T>> Find(ConVars& service, std::string_view name);

    /** The current server-side value. `T{}` when this handle never resolved. */
    T Get() const;

    /** Set @p value through the server console. Error::NotReady when the handle never resolved. */
    Status Set(const T& value);

    /**
     * Send `CNETMsg_SetConVar` to one client so its prediction uses @p value.
     *
     * Only that client's replicated view changes - the server value is untouched and no other
     * client is affected. The client's snapshot on connect (and on map change) restores the
     * server value, so re-send from a PlayerSpawn listener to keep an override sticky.
     */
    Status SetFor(int slot, const T& value) const;

    /** Flip to @p value without callbacks or networking until the returned scope dies. */
    [[nodiscard]] ConVarRawScope<T> RawScope(const T& value)
        requires(!std::is_same_v<T, std::string>)
    {
        return ConVarRawScope<T>(*this, value);
    }

    /** The convar's name, borrowed from this handle. */
    std::string_view Name() const { return _name; }

    /** Whether @ref Find resolved this handle. */
    explicit operator bool() const noexcept { return _storage != nullptr; }

private:
    friend class ConVarRawScope<T>;

    Status SetRaw(const T& value)
        requires(!std::is_same_v<T, std::string>);

    ConVars* _service = nullptr;
    std::string _name;
    void* _storage = nullptr;  ///< the convar's CVValue_t* (void* so this header stays SDK-free)
    int16_t _type = -1;        ///< the engine's EConVarType, so Get reads the right width
};

/**
 * @brief Console access to the engine's convar system, and the source of @ref ConVar handles.
 *
 * Reading and writing a specific convar goes through @ref ConVar; this service owns the parts
 * that are not about one convar - running a console line, and reporting every change the engine
 * makes.
 */
class ConVars
{
public:
    /** @p interfaces supplies ICvar, IVEngineServer2 and the message systems; it must outlive this
     *  service, which reaches for it again from its destructor. */
    explicit ConVars(Interfaces& interfaces);
    ~ConVars();
    ConVars(const ConVars&) = delete;
    ConVars& operator=(const ConVars&) = delete;

    /** Confirm ICvar is live. Error::NotReady when it is not. */
    Status Initialize();

    /** Queue @p command on the server console, as if it were a line in a cfg. */
    void ExecuteServerCommand(std::string_view command);

    /**
     * @brief Every engine-side convar change, for as long as anything is subscribed.
     *
     * The first subscription installs ICvar's global change callback and the last one to drop
     * removes it, so nothing is hooked while nobody is listening. Each plugin has its own copy of
     * this service and its own trampoline, so plugins loaded together all see every change.
     */
    Event<const ConVarChange&> Changed;

private:
    // ConVar<T>::SetFor is the only caller; per-client replication is a property of one convar,
    // not a service-level verb, so it is not part of this class's public surface.
    template <class U>
    friend class ConVar;

    /** Post CNETMsg_SetConVar for @p name to @p slot alone. */
    bool SendToClient(int slot, std::string_view name, std::string_view value);

    /** The cached CNETMsg_SetConVar prototype, looked up on first use. Null when unavailable. */
    INetworkMessageInternal* SetConVarMessage();

    /** @ref Changed's Lifecycle. False when ICvar is not resolved, which leaves it unarmed. */
    bool RouteChanges();
    void StopRoutingChanges();

    Interfaces& _interfaces;
    bool _routingChanges = false;
    INetworkMessageInternal* _setConVarMsg = nullptr;
};

/**
 * The console text for @p value: what a cfg line or a `CNETMsg_SetConVar` payload carries.
 *
 * Bools are "1"/"0" rather than "true"/"false" - the console accepts both, but the replicated
 * message reaches the client's own parser, which does not. Only the four convar types exist; the
 * primary template is deliberately undefined so any other `T` fails to link rather than compiling
 * into something the engine cannot parse.
 */
template <class T>
std::string ConVarText(const T& value);

template <>
std::string ConVarText<bool>(const bool& value);
template <>
std::string ConVarText<int>(const int& value);
template <>
std::string ConVarText<float>(const float& value);
template <>
std::string ConVarText<std::string>(const std::string& value);

}  // namespace VoltMod
