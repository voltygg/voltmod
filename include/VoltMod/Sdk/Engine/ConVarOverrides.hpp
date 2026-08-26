#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Sdk
{

class ConVarService;

/**
 * @brief Takes over server convars and gives them back exactly as they were.
 *
 * A feature that changes a server-wide convar owns two obligations that are easy to get subtly
 * wrong: snapshot the operator's value before the *first* write, and on the way out restore only
 * what it actually took, so a server that never triggered the feature keeps its own cfg. This
 * holds that ledger and nothing else.
 *
 * Writes go through the server console line rather than @ref ConVarService::SetFloat, because
 * FCVAR_REPLICATED convars set directly reach only the server's stored value - clients keep
 * predicting the old one and their movement or damage disagrees with the server.
 *
 * Names and values are owned copies, so a caller may pass a temporary and nothing here outlives
 * its own storage. Values are held as strings, which is what the console path takes anyway and
 * what lets one ledger cover float, int and bool convars without caring which is which.
 *
 * Not a replicator: pushing a value to one client's prediction is
 * @ref ConVarService::ReplicateToClient.
 */
class ConVarOverrides
{
public:
    /** @p conVars must outlive this ledger; the destructor reaches for it to restore. */
    explicit ConVarOverrides(ConVarService& conVars) : _conVars(conVars) {}
    ~ConVarOverrides() { ReleaseAll(); }
    ConVarOverrides(const ConVarOverrides&) = delete;
    ConVarOverrides& operator=(const ConVarOverrides&) = delete;

    /**
     * Set @p name to @p value, snapshotting the live value on the first take.
     *
     * Re-taking a convar this ledger already holds re-asserts @p value without re-snapshotting,
     * which is what makes this safe to call every round - the engine resets convars around a map
     * change, so an override that is not re-asserted silently lapses.
     *
     * @p fallback is recorded as the saved value only when the live one cannot be read at all.
     */
    void Take(std::string_view name, std::string_view value, std::string_view fallback = "0");

    /** Convenience for numeric convars; formats @p value and @p fallback and calls the above. */
    void Take(std::string_view name, float value, float fallback = 0.0f);

    /** Put @p name back as it was and forget it. A no-op when this ledger never took it. */
    void Release(std::string_view name);

    /** Release every held convar, in the order they were taken. */
    void ReleaseAll();

    bool Holds(std::string_view name) const { return IndexOf(name).has_value(); }

    /** How many convars are currently taken over. */
    std::size_t Count() const { return _held.size(); }

private:
    struct Entry
    {
        std::string Name;
        std::string Saved;  // the operator's value, as read before the first write
    };

    /** Position of @p name in @ref _held, or nullopt when this ledger does not hold it. */
    std::optional<std::size_t> IndexOf(std::string_view name) const;
    void Write(std::string_view name, std::string_view value);

    ConVarService& _conVars;
    // Linear scan: a feature holds a handful of convars, never enough to index.
    std::vector<Entry> _held;
};

}  // namespace VoltMod::Sdk
