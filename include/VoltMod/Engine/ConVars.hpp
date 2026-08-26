#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class INetworkMessageInternal;

namespace VoltMod::Engine
{

struct Interfaces;

/**
 * @brief Direct handle to a convar's value storage.
 *
 * Reads and writes bypass change callbacks and FCVAR_REPLICATED networking: nothing is sent
 * to clients and no OnChange listener fires. Intended for scoped flips around one player's
 * processing (e.g. inside a Hooks::Movement pre/post pair) where the engine setters' broadcast-
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

    using ChangeCallback = std::function<void(const char* name, const char* oldValue, const char* newValue)>;

    /**
     * Listen for engine-side convar changes until the returned Subscription drops.
     *
     * The engine's global change callback is installed on the first subscription across all
     * services and removed once the last one goes away, so plugins loaded together each see
     * every change regardless of load order.
     */
    [[nodiscard]] Core::Subscription OnChange(ChangeCallback callback);

    /** Fan a change out to this service's listeners. For the engine trampoline only. */
    void DispatchChange(const char* name, const char* oldValue, const char* newValue);

    /** Drop every listener and stop routing engine changes here. Idempotent; the destructor
     *  calls it. Other services keep receiving changes. */
    void Shutdown();

private:
    /** The cached CNETMsg_SetConVar prototype, looked up on first use. Null when unavailable. */
    INetworkMessageInternal* SetConVarMessage();

    Interfaces& _interfaces;
    Core::CallbackRegistry<ChangeCallback> _changeCallbacks;
    bool _routingChanges = false;
    INetworkMessageInternal* _setConVarMsg = nullptr;
};

}  // namespace VoltMod::Engine
