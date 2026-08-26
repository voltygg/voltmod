#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

/**
 * @brief Direct handle to a convar's value storage.
 *
 * Reads and writes bypass change callbacks and FCVAR_REPLICATED networking: nothing is sent
 * to clients and no OnChange listener fires. Intended for scoped flips around one player's
 * processing (e.g. inside a Movement pre/post pair) where the engine setters' broadcast-
 * to-everyone behavior would be wrong; the caller must restore the prior value itself.
 *
 * There is no type checking - use the accessor matching the convar's actual engine type.
 * The handle stays valid for the convar's lifetime (registered convars outlive map changes),
 * so it can be resolved once and cached.
 */
class ConVarStorage
{
public:
    ConVarStorage() = default;
    explicit ConVarStorage(const char* name);

    bool IsValid() const { return _value != nullptr; }
    bool GetBool() const;
    void SetBool(bool value);
    float GetFloat() const;
    void SetFloat(float value);

private:
    void* _value = nullptr;  // the convar's CVValue_t* (kept as void* so this header stays SDK-free)
};

/**
 * @brief Typed wrapper around ICvar for finding, reading, writing, and listening to ConVars.
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

    bool Initialize();

    /** Reads return nullopt for a null, unknown, or not-yet-registered convar. */
    std::optional<int> GetInt(const char* name) const;
    std::optional<float> GetFloat(const char* name) const;
    std::optional<std::string> GetString(const char* name) const;
    std::optional<bool> GetBool(const char* name) const;
    bool Exists(const char* name) const;

    /**
     * Direct sets: change only the server's stored value - no networking (FCVAR_REPLICATED
     * convars won't reach clients; use ExecuteServerCommand for cfg-line semantics) and no
     * cross-type conversion (the SDK's SetAs<T> silently no-ops on a differently-typed convar,
     * e.g. SetInt on a bool convar like sv_autobunnyhopping - use SetString for those).
     */
    bool SetInt(const char* name, int value);
    bool SetFloat(const char* name, float value);
    bool SetString(const char* name, const char* value);

    /** Queue @p command on the server console, as if it were a line in a cfg. */
    void ExecuteServerCommand(std::string_view command);

    /** Value-storage handle for @p name (see @ref ConVarStorage). !IsValid() when unknown. */
    ConVarStorage Storage(const char* name) const { return ConVarStorage(name); }

    /**
     * @brief Send CNETMsg_SetConVar to one client so its prediction uses @p value.
     *
     * Only that client's replicated view changes - the server-side value is untouched and no
     * other client is affected. The client's snapshot on connect (and on map change) restores
     * the server value, so re-send from a PlayerSpawn listener to keep the override sticky.
     * @return false when the message system or slot is unavailable.
     */
    bool ReplicateToClient(int slot, const char* name, const char* value);

    /**
     * @brief Every engine-side convar change, for as long as anything is subscribed.
     *
     * The first subscription installs ICvar's global change callback and the last one to drop
     * removes it, so nothing is hooked while nobody is listening. Each plugin has its own copy of
     * this service and its own trampoline, so plugins loaded together all see every change.
     */
    Event<const ConVarChange&> Changed;

private:
    /** The cached CNETMsg_SetConVar prototype, looked up on first use. Null when unavailable. */
    INetworkMessageInternal* SetConVarMessage();

    /** @ref Changed's Lifecycle. False when ICvar is not resolved, which leaves it unarmed. */
    bool RouteChanges();
    void StopRoutingChanges();

    Interfaces& _interfaces;
    bool _routingChanges = false;
    INetworkMessageInternal* _setConVarMsg = nullptr;
};

}  // namespace VoltMod
