#include <VoltMod/Engine/ConVarLease.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <format>
#include <string>

namespace VoltMod
{

bool ConVarLease::Override(std::string_view name, std::string_view value)
{
    if (name.empty())
        return false;

    // Save only on the first take. A re-take is a re-assert (see the header on map changes), and
    // re-reading here would save the override as if it were the operator's own value.
    if (!_saved.Contains(name))
    {
        auto live = _conVars.GetString(std::string(name).c_str());
        if (!live)
            return false;

        _saved.Save(name, *live);
    }

    Write(name, value);
    return true;
}

bool ConVarLease::Override(std::string_view name, float value)
{
    return Override(name, std::format("{}", value));
}

void ConVarLease::Restore(std::string_view name)
{
    if (auto value = _saved.Remove(name))
        Write(name, *value);
}

void ConVarLease::RestoreAll()
{
    for (const auto& entry : _saved.Entries())
        Write(entry.Name, entry.Value);
    _saved.Clear();
}

void ConVarLease::Write(std::string_view name, std::string_view value)
{
    _conVars.ExecuteServerCommand(std::format("{} {}", name, value));
}

}  // namespace VoltMod
