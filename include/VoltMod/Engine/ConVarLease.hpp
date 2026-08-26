#pragma once

#include <VoltMod/Engine/ConVarSnapshots.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <cstddef>
#include <string>
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
 * Writes go through @ref SetMode::Console, because an FCVAR_REPLICATED convar set server-side only
 * reaches the server's stored value - clients keep predicting the old one and their movement or
 * damage disagrees with the server.
 *
 * Not a replicator: pushing a value to one client's prediction is @ref ConVar::SetFor.
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
     * Set @p cvar to @p value, saving the live value on the first take.
     *
     * Overriding a convar this already holds re-asserts @p value without re-saving, which is what
     * makes this safe to call every round - the engine resets convars around a map change, so an
     * override that is not re-asserted silently lapses.
     *
     * @return false when @p cvar never resolved. Nothing is recorded in that case, so @ref Count
     *         cannot report a phantom that @ref RestoreAll would then write to nothing.
     */
    template <class T>
    bool Override(ConVar<T>& cvar, const T& value)
    {
        if (!cvar.IsValid())
            return false;

        // Save only on the first take. A re-take is a re-assert (see above on map changes), and
        // re-reading here would save the override as if it were the operator's own value.
        if (!_saved.Contains(cvar.Name()))
            _saved.Save(cvar.Name(), ConVarText(cvar.Get()));

        Write(cvar.Name(), ConVarText(value));
        return true;
    }

    /** Put @p cvar back as it was and forget it. A no-op when this never took it. */
    template <class T>
    void Restore(const ConVar<T>& cvar)
    {
        Restore(cvar.Name());
    }

    /** Restore every held convar, in the order they were taken. */
    void RestoreAll();

    template <class T>
    bool IsOverridden(const ConVar<T>& cvar) const
    {
        return _saved.Contains(cvar.Name());
    }

    /** How many convars are currently taken over. */
    std::size_t Count() const { return _saved.Size(); }

private:
    /** Snapshots are keyed by name because that is what the console path writes; the typed handle
     *  is the caller's, not this class's, so only the name and the rendered text cross over. */
    void Restore(std::string_view name);
    void Write(std::string_view name, std::string_view value);

    ConVars& _conVars;
    ConVarSnapshots _saved;
};

}  // namespace VoltMod
