#include <VoltMod/Engine/ConVars/ConVarOverrides.hpp>
#include <VoltMod/Engine/ConVars/ConVars.hpp>
#include <algorithm>
#include <string>

namespace VoltMod
{

void ConVarOverrides::Restore(std::string_view name)
{
    const auto it = std::ranges::find(_saved, name, &Snapshot::Name);
    if (it == _saved.end())
        return;

    Write(it->Name, it->Value);
    _saved.erase(it);
}

void ConVarOverrides::RestoreAll()
{
    for (const auto& entry : _saved)
        Write(entry.Name, entry.Value);
    _saved.clear();
}

void ConVarOverrides::Write(std::string_view name, std::string_view value)
{
    // A snapshot restores whatever the value was, spaces and semicolons included; SetByConsole
    // owns the quoting. Restoration is best effort during engine shutdown.
    (void)_conVars.SetByConsole(name, value);
}

}  // namespace VoltMod
