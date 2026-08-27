#include <VoltMod/Engine/ConVarOverrides.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <algorithm>
#include <format>
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
    _conVars.ExecuteServerCommand(std::format("{} {}", name, value));
}

}  // namespace VoltMod
