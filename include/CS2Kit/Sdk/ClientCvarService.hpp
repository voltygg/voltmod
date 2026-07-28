#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace CS2Kit::Sdk
{

/** @brief How the client answered a convar query (CCLCMsg_RespondCvarValue::status_code). */
enum class ClientCvarStatus
{
    ValueIntact = 0,  ///< The client reported the convar's current value.
    CvarNotFound,     ///< The client has no convar by that name.
    NotACvar,         ///< The name exists on the client but is a command, not a convar.
    CvarProtected     ///< The convar is marked protected; the client withholds its value.
};

/** Lower-case identifier for @p status ("value_intact", "cvar_not_found", ...). */
std::string_view ToString(ClientCvarStatus status);

/**
 * @brief Asks a connected client what one of its own convars is set to.
 *
 * The server sends `CSVCMsg_GetCvarValue` carrying a cookie; the client answers with
 * `CCLCMsg_RespondCvarValue` some round-trips later, which the kit intercepts through a manual
 * vtable hook on `CServerSideClient` and routes back to the callback that asked. Queries are
 * therefore asynchronous, unordered, and never guaranteed to complete - a client that disconnects
 * or simply ignores the request produces no callback at all, so callers must not treat a query as
 * a request/response pair with a deadline of their own.
 *
 * Reads are trust-on-the-client by nature: a modified client can answer with anything. Treat the
 * result as evidence, not proof.
 *
 * The service is a degradable load stage. It depends on two gamedata offsets
 * (`ProcessRespondCvarValue`, `ServerSideClientSlot`) and an RTTI/symbol lookup of the
 * `CServerSideClient` vtable, all of which drift with engine updates. When any of that fails the
 * kit logs one warning, @ref Available() stays false, and every Query() returns false.
 *
 * @code
 * Engine().ClientCvars.Query(slot, "sensitivity",
 *     [](int slot, ClientCvarStatus status, std::string_view name, std::string_view value) {
 *         if (status == ClientCvarStatus::ValueIntact)
 *             Log::Info("{} = {}", name, value);
 *     });
 * @endcode
 */
class ClientCvarService
{
public:
    /**
     * Invoked on the game thread when the client answers. @p name and @p value borrow the decoded
     * message and are only valid for the duration of the call; copy what you keep. @p value is
     * empty unless @p status is ClientCvarStatus::ValueIntact.
     */
    using QueryCallback =
        std::function<void(int slot, ClientCvarStatus status, std::string_view name, std::string_view value)>;

    ClientCvarService();
    ~ClientCvarService();
    ClientCvarService(const ClientCvarService&) = delete;
    ClientCvarService& operator=(const ClientCvarService&) = delete;

    /** Resolve the offsets and install the response hook. Idempotent; false leaves the service inert. */
    bool Initialize();

    /** Remove the response hook and drop every pending query. Idempotent; also runs from the destructor. */
    void Shutdown();

    /** True once Initialize() has succeeded. Query() only works while this holds. */
    bool Available() const;

    /**
     * Ask @p slot for its value of @p cvarName.
     *
     * False when the service is unavailable, the slot holds a bot or nobody, the per-slot pending
     * cap is reached, or the message could not be sent. A query for a convar already in flight for
     * that slot re-targets the outstanding request instead of sending a second one, so polling the
     * same convar cannot flood a client. Pending entries expire silently after 10 seconds.
     */
    bool Query(int slot, const std::string& cvarName, QueryCallback callback);

    /** Number of queries awaiting an answer on @p slot. Diagnostics only. */
    size_t PendingCount(int slot) const;

    /** Drop anything the slot's previous occupant left behind. Called by the kit's connect path. */
    void OnClientFullyConnect(int slot);

    /** Drop @p slot's pending queries; their callbacks will never fire. Called by the kit. */
    void OnPlayerDisconnect(int slot);

    /** Drop every pending query for the new map. Called by the kit's StartupServer hook. */
    void OnServerStartup();

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace CS2Kit::Sdk
