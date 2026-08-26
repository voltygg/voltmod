#include <VoltMod/Engine/ConVarLease.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <format>
#include <string>

namespace VoltMod
{

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
