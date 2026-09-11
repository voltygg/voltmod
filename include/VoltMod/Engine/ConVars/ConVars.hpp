#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Convar change whose string views remain valid only during the handler. */
struct ConVarChange
{
    std::string_view Name;
    std::string_view OldValue;
    std::string_view NewValue;
};

/** The value types a @ref ConVar handle supports. Naming one the engine does not store this way
 *  fails at the `Find` call rather than at link time. */
template <class T>
concept ConVarValue =
    std::same_as<T, bool> || std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, std::string>;

/** A @ref ConVarValue whose storage can be written behind the engine's back: everything but a
 *  string, which has no fixed-width slot to poke. @ref ConVar::RawScope and the raw writes it
 *  depends on are limited to these. */
template <class T>
concept RawConVarValue = ConVarValue<T> && !std::same_as<T, std::string>;

template <class T>
class ConVar;

/** Temporary raw convar value restored on destruction. The handle must outlive the scope. */
template <class T>
class ConVarRawScope
{
public:
    /** Set now and restore the current value on destruction. */
    ConVarRawScope(ConVar<T>& cvar, const T& value)
        requires RawConVarValue<T>;
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
 * Typed convar handle resolved once by name. Supported types are `bool`, `int`, `float`, and
 * `std::string`.
 * Handles remain valid across map changes. Unresolved handles read as `T{}` and
 * reject writes.
 */
template <class T>
class ConVar
{
public:
    ConVar() = default;

    /** Current server value, or `T{}` when unresolved. */
    T Get() const;

    /** Set through the server console. Success means queued; Error::NotReady means unavailable. */
    Status Set(const T& value);

    /**
     * Override one client's replicated view without changing the server value. Connect and map
     * snapshots
     * replace the override, so resend it after spawn when persistence is required.
     */
    Status SetFor(int slot, const T& value) const;

    /** Set without callbacks or networking until the returned scope is destroyed. */
    [[nodiscard]] ConVarRawScope<T> RawScope(const T& value)
        requires RawConVarValue<T>
    {
        return ConVarRawScope<T>(*this, value);
    }

    std::string_view Name() const { return _name; }

    explicit operator bool() const noexcept { return _storage != nullptr; }

private:
    friend class ConVarRawScope<T>;
    friend class ConVars;

    Status SetRaw(const T& value)
        requires RawConVarValue<T>;

    ConVars* _service = nullptr;
    std::string _name;
    void* _storage = nullptr;  ///< the convar's CVValue_t* (void* so this header stays SDK-free)
    int16_t _type = -1;        ///< the engine's EConVarType, so Get reads the right width
};

/** Convar lookup, console commands, client overrides, and global change events. */
class ConVars
{
public:
    /** @p interfaces must outlive this service. */
    explicit ConVars(Interfaces& interfaces);
    ~ConVars();
    ConVars(const ConVars&) = delete;
    ConVars& operator=(const ConVars&) = delete;

    /** Return Error::NotReady when ICvar is unavailable. */
    Status Initialize();

    /** Resolve by name. Returns NotFound when absent and Invalid on a type mismatch. */
    template <ConVarValue T>
    Result<ConVar<T>> Find(std::string_view name);

    /** Queue a server console line. Returns Error::NotReady when IVEngineServer2 is unavailable. */
    Status ExecuteServerCommand(std::string_view command);

    /**
     * @brief Assign a convar over the console, quoting the value.
     *
     * The one place the `name value` console line is built, so the quoting rule is not
     * rediscovered per call site: a space would truncate the value and a `;` would start a
     * second command. A replicated payload sent straight to a client stays unquoted.
     */
    Status SetByConsole(std::string_view name, std::string_view value);

    /** All engine convar changes. The global callback exists only while this event has subscribers. */
    Event<const ConVarChange&> Changed;

private:
    template <class U>
    friend class ConVar;

    bool SendToClient(int slot, std::string_view name, std::string_view value);

    INetworkMessageInternal* SetConVarMessage();

    bool RouteChanges();
    void StopRoutingChanges();

    Interfaces& _interfaces;
    bool _routingChanges = false;
    INetworkMessageInternal* _setConVarMsg = nullptr;
};

/** Console and network text for a supported convar value. Bools use `1` and `0` for clients. */
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
