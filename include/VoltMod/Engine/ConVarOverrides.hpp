#pragma once

#include <VoltMod/Engine/ConVars.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * Server-wide convar overrides that restore the first observed value on destruction.
 * Writes use the console so FCVAR_REPLICATED changes reach clients. Use @ref ConVar::SetFor for
 * one client.
 */
class ConVarOverrides
{
public:
    /** @p conVars must outlive this object. */
    explicit ConVarOverrides(ConVars& conVars) : _conVars(conVars) {}
    ~ConVarOverrides() { RestoreAll(); }
    ConVarOverrides(const ConVarOverrides&) = delete;
    ConVarOverrides& operator=(const ConVarOverrides&) = delete;

    /** Set and snapshot on first use. Later calls reapply without replacing the snapshot. */
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

    /** Restore and forget one override. */
    template <class T>
    void Restore(const ConVar<T>& cvar)
    {
        Restore(cvar.Name());
    }

    /** Restore all overrides in insertion order. */
    void RestoreAll();

private:
    struct Snapshot
    {
        std::string Name;
        std::string Value;
    };

    // Names remain valid if callers move or destroy their handles.
    void Restore(std::string_view name);
    void Write(std::string_view name, std::string_view value);

    ConVars& _conVars;
    std::vector<Snapshot> _saved;
};

}  // namespace VoltMod
