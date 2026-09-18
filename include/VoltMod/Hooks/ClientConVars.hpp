#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** @brief How the client answered a convar query (CCLCMsg_RespondCvarValue::status_code). */
enum class ClientConVarStatus
{
    Answered = 0,  ///< The client reported the convar's current value.
    NotFound,      ///< The client has no convar by that name.
    NotAConVar,    ///< The name exists on the client but is a command, not a convar.
    Protected      ///< The convar is marked protected; the client withholds its value.
};

/**
 * @brief Asks a connected client what one of its own convars is set to.
 *
 * The server sends `CSVCMsg_GetCvarValue` with a cookie. The client answers later with
 * `CCLCMsg_RespondCvarValue`, intercepted through a vtable hook on `CServerSideClient`.
 * Queries are asynchronous, unordered, and may never complete.
 *
 * A modified client can answer with anything, so treat the result as evidence, not proof.
 *
 * This optional load step depends on the `CServerSideClient::ProcessRespondCvarValue` vtable slot,
 * the `CServerSideClientBase::m_nClientSlot` offset, and an RTTI or symbol lookup of the
 * `CServerSideClient` vtable. These values drift with engine updates. On failure @ref Available
 * carries the reason.
 *
 * @code
 * runtime.Hooks.ClientConVars.Query(slot, "sensitivity",
 *     [](int slot, ClientConVarStatus status, std::string_view name, std::string_view value) {
 *         if (status == ClientConVarStatus::Answered)
 *             Log::Info("{} = {}", name, value);
 *     });
 * @endcode
 */
class ClientConVars
{
public:
    /**
     * Invoked on the game thread when the client answers. @p name and @p value borrow the decoded
     * message, so copy what you keep. @p value is empty unless @p status is Answered.
     */
    using QueryCallback =
        std::function<void(int slot, ClientConVarStatus status, std::string_view name, std::string_view value)>;

    /** @p interfaces and @p bindings drive the response hook and the query send path. @p slots
     *  tells the service when a slot changes hands, so an answer can never reach the callback of
     *  whoever held the slot before. All three must outlive it; the Runtime declares them above. */
    ClientConVars(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots);
    ~ClientConVars();
    ClientConVars(const ClientConVars&) = delete;
    ClientConVars& operator=(const ClientConVars&) = delete;

    /** Install the response hook. Idempotent; errors leave the service inert. */
    Status Initialize();

    /** Why queries cannot be sent: the error Initialize returned, or that it has not run. */
    Status Available() const;

    /** Remove the hook and drop every pending query. Idempotent; also runs from the destructor. */
    void Shutdown();

    /**
     * Ask @p slot for its value of @p cvarName. False when the service is not @ref Available, the
     * slot holds a bot or nobody, the per-slot pending cap is reached, or the message could not
     * be sent.
     *
     * A convar already in flight for that slot re-targets the outstanding request rather than
     * sending a second one, so polling cannot flood a client. Pending entries expire silently
     * after 10 seconds.
     */
    bool Query(int slot, const std::string& cvarName, QueryCallback callback);

    /** Number of queries awaiting an answer on @p slot. Diagnostics only. */
    size_t PendingCount(int slot) const;

    /** Drop anything the slot's previous occupant left behind. Called by the framework's connect path. */
    void OnClientFullyConnect(int slot);

    /** Drop every pending query for the new map. Called by the framework's StartupServer hook. */
    void OnServerStartup();

private:
    Status Install();

    /** Deliver one CCLCMsg_RespondCvarValue, the message type the response hook carries. */
    void OnRespondCvarValue(const void* client, const void* message);

    /** Send a query to one connected human client. */
    bool Send(int slot, const std::string& cvarName, int cookie);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    /** Behind a pointer only so its header stays under src/, where its tests live. */
    std::unique_ptr<PendingConVarQueries> _pending;
    INetworkMessageInternal* _getCvarValue = nullptr;
    Error _failure = Error::NotReady("client convar queries are not initialized");
    Subscription _hook;
    /** Declared after _pending so it unregisters before the table its callback clears. */
    Subscription _slotListener;
};

}  // namespace VoltMod
