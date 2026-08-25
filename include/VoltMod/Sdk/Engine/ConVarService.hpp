#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

class INetworkMessageInternal;

namespace VoltMod::Sdk
{

struct GameInterfaces;

/**
 * @brief Direct handle to a convar's raw value storage.
 *
 * Reads and writes bypass change callbacks and FCVAR_REPLICATED networking: nothing is sent
 * to clients and no OnChange listener fires. Intended for scoped flips around one player's
 * processing (e.g. inside a MovementHook pre/post pair) where the engine setters' broadcast-
 * to-everyone behavior would be wrong; the caller must restore the prior value itself.
 *
 * There is no type checking - use the accessor matching the convar's actual engine type.
 * The handle stays valid for the convar's lifetime (registered convars outlive map changes),
 * so it can be resolved once and cached.
 */
class RawConVar
{
public:
    RawConVar() = default;
    explicit RawConVar(const char* name);

    bool Valid() const { return _value != nullptr; }
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
class ConVarService
{
public:
    /** @p interfaces supplies ICvar, IVEngineServer2 and the message systems; it must outlive this
     *  service, which reaches for it again from its destructor. */
    explicit ConVarService(GameInterfaces& interfaces);
    ~ConVarService();
    ConVarService(const ConVarService&) = delete;
    ConVarService& operator=(const ConVarService&) = delete;

    bool Initialize();

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

    void ExecuteServerCommand(const char* command);

    /** Raw-storage handle for @p name (see @ref RawConVar). !Valid() when the convar is unknown. */
    RawConVar Raw(const char* name) const { return RawConVar(name); }

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
    [[nodiscard]] Core::Subscription OnChange(ChangeCallback callback);
    void DispatchChange(const char* name, const char* oldValue, const char* newValue);

    /** Take the engine's global change callback back off, and stop routing to this instance.
     *  Idempotent; the destructor calls it. */
    void Shutdown();

private:
    GameInterfaces& _interfaces;
    Core::CallbackRegistry<ChangeCallback> _changeCallbacks;
    bool _globalCallbackInstalled = false;
    INetworkMessageInternal* _setConVarMsg = nullptr;
};

}  // namespace VoltMod::Sdk
