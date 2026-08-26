#pragma once

#include <VoltMod/Engine/ConVarSnapshots.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <cstddef>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Takes over server convars and gives them back exactly as they were.
 *
 * A feature that changes a server-wide convar owns two obligations that are easy to get subtly
 * wrong: save the operator's value before the *first* write, and on the way out restore only what
 * it actually took, so a server that never triggered the feature keeps its own cfg. This holds
 * those snapshots and nothing else.
 *
 * Writes go through the server console line rather than @ref ConVars::SetFloat, because
 * FCVAR_REPLICATED convars set directly reach only the server's stored value - clients keep
 * predicting the old one and their movement or damage disagrees with the server.
 *
 * Not a replicator: pushing a value to one client's prediction is
 * @ref ConVars::ReplicateToClient.
 */
class ConVarLease
{
public:
    /** @p conVars must outlive this object; the destructor reaches for it to restore. */
    explicit ConVarLease(ConVars& conVars) : _conVars(conVars) {}
    ~ConVarLease() { RestoreAll(); }
    ConVarLease(const ConVarLease&) = delete;
    ConVarLease& operator=(const ConVarLease&) = delete;

    /**
     * Set @p name to @p value, saving the live value on the first take.
     *
     * Overriding a convar this already holds re-asserts @p value without re-saving, which is what
     * makes this safe to call every round - the engine resets convars around a map change, so an
     * override that is not re-asserted silently lapses.
     *
     * @return false when @p name is empty or names a convar the server does not have. A convar
     *         that cannot be read is one the console cannot set either, so it is refused rather
     *         than recorded as a phantom that @ref Count would report and @ref RestoreAll would
     *         write to nothing.
     */
    bool Override(std::string_view name, std::string_view value);

    /** Convenience for numeric convars; formats @p value and calls the above. */
    bool Override(std::string_view name, float value);

    /** Put @p name back as it was and forget it. A no-op when this never took it. */
    void Restore(std::string_view name);

    /** Restore every held convar, in the order they were taken. */
    void RestoreAll();

    bool IsOverridden(std::string_view name) const { return _saved.Contains(name); }

    /** How many convars are currently taken over. */
    std::size_t Count() const { return _saved.Size(); }

private:
    void Write(std::string_view name, std::string_view value);

    ConVars& _conVars;
    ConVarSnapshots _saved;
};

}  // namespace VoltMod
