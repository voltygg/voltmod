#pragma once

#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Sdk/ClientCvarService.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CS2Kit::Sdk::Detail
{

/** @brief One outstanding client convar query, keyed by the cookie sent to the client. */
struct PendingCvarQuery
{
    std::string Name;                           ///< Convar name the query asked for.
    ClientCvarService::QueryCallback Callback;  ///< Invoked once, when a matching answer arrives.
    double SentAtSec = 0.0;                     ///< Caller-supplied monotonic timestamp of the send.
};

/**
 * @brief Bookkeeping for @ref ClientCvarService: per-slot, cookie-keyed queries awaiting an answer.
 *
 * Split out of the service so the matching and expiry rules are testable without the SDK. Time is
 * caller-supplied (seconds, any monotonic origin) and expiry is lazy - nothing is dropped until the
 * next Prune() for that slot, which the service runs on every Query().
 *
 * A client is under no obligation to answer, so the table is capped per slot: once
 * @ref MaxPendingPerSlot queries are outstanding and unexpired, further ones are refused rather
 * than queued behind a silent client.
 */
class ClientCvarPendingTable
{
public:
    /** Outstanding queries allowed per slot before Query() starts refusing. */
    static constexpr size_t MaxPendingPerSlot = 11;

    /** Age at which an unanswered query is dropped, in seconds. */
    static constexpr double TimeoutSec = 10.0;

    /** Largest cookie handed out; the protobuf field is an int32. */
    static constexpr uint32_t MaxCookie = 0x7FFFFFFF;

    /** Drop @p slot's queries older than @ref TimeoutSec. Their callbacks never fire. */
    void Prune(int slot, double now);

    /**
     * Hand @p callback to the query for @p name already in flight on @p slot, replacing its
     * previous callback. False when nothing is in flight for that name, in which case @p callback
     * is left untouched and the caller should send a new query.
     */
    bool Retarget(int slot, std::string_view name, ClientCvarService::QueryCallback& callback);

    /** True when @p slot has no room for another query. */
    bool Full(int slot) const;

    /**
     * Next cookie unused on @p slot, or -1 when none could be found. Cookies increase
     * monotonically and wrap back to 1 at @ref MaxCookie.
     */
    int NextCookie(int slot);

    void Add(int slot, int cookie, std::string name, ClientCvarService::QueryCallback callback, double now);

    /**
     * Remove and return @p slot's query @p cookie, but only when @p name is the convar it asked
     * for - a client that answers with a different name is answering a question nobody posed.
     */
    std::optional<PendingCvarQuery> Take(int slot, int cookie, std::string_view name);

    void Clear(int slot);
    void ClearAll();

    size_t Count(int slot) const;

private:
    std::array<std::unordered_map<int, PendingCvarQuery>, Core::MaxPlayers> _slots;
    uint32_t _cookieCounter = 0;
};

}  // namespace CS2Kit::Sdk::Detail
