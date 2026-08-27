#pragma once

#include <VoltMod/Engine/ConVars.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

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
 * Writes go through the console, because an FCVAR_REPLICATED convar set server-side only
 * reaches the server's stored value - clients keep predicting the old one and their movement or
 * damage disagrees with the server.
 *
 * Not a replicator: pushing a value to one client's prediction is @ref ConVar::SetFor.
 */
class ConVarOverrides
{
public:
    /** @p conVars must outlive this object; the destructor reaches for it to restore. */
    explicit ConVarOverrides(ConVars& conVars) : _conVars(conVars) {}
    ~ConVarOverrides() { RestoreAll(); }
    ConVarOverrides(const ConVarOverrides&) = delete;
    ConVarOverrides& operator=(const ConVarOverrides&) = delete;

    /**
     * Set @p cvar to @p value, saving the live value on the first take.
     *
     * Overriding a convar this already holds re-asserts @p value without re-saving, which is what
     * makes this safe to call every round - the engine resets convars around a map change, so an
     * override that is not re-asserted silently lapses.
     *
     * @return false when @p cvar never resolved.
     */
    template <class T>
    bool Set(ConVar<T>& cvar, const T& value)
    {
        if (!cvar)
            return false;

        const auto name = cvar.Name();
        if (std::ranges::find(_saved, name, &Snapshot::Name) == _saved.end())
            _saved.push_back({.Name = std::string(name), .Value = ConVarText(cvar.Get())});

        return cvar.Set(value).has_value();
    }

    /** Put @p cvar back as it was and forget it. A no-op when this never took it. */
    template <class T>
    void Restore(const ConVar<T>& cvar)
    {
        Restore(cvar.Name());
    }

    /** Restore every held convar, in the order they were taken. */
    void RestoreAll();

private:
    struct Snapshot
    {
        std::string Name;
        std::string Value;
    };

    // Snapshots use names because callers own the typed handles and may move or destroy them
    // before the owner. A feature only holds a handful, so a vector keeps lookup and restore order
    // in one small structure.
    void Restore(std::string_view name);
    void Write(std::string_view name, std::string_view value);

    ConVars& _conVars;
    std::vector<Snapshot> _saved;
};

}  // namespace VoltMod
