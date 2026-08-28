#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Map.hpp>
#include <eiface.h>
#include <format>
#include <string>

namespace VoltMod
{

Map::Map(Interfaces& interfaces, ConVars& conVars) : _interfaces(interfaces), _conVars(conVars) {}

bool Map::IsValid(std::string_view name) const
{
    if (name.empty())
        return false;

    auto* engine = _interfaces.Engine;
    if (!engine)
    {
        Log::Warn("Map::IsValid: IVEngineServer2 not available.");
        return false;
    }

    return engine->IsMapValid(std::string(name).c_str()) != 0;
}

bool Map::ChangeLevel(std::string_view name)
{
    if (!IsValid(name))
    {
        Log::Warn("Map::ChangeLevel: '{}' is not a loadable map.", name);
        return false;
    }

    // Landmark is used only by save-game transitions.
    _interfaces.Engine->ChangeLevel(std::string(name).c_str(), nullptr);
    return true;
}

bool Map::ChangeToWorkshop(uint64_t workshopId)
{
    if (workshopId == 0)
    {
        Log::Warn("Map::ChangeToWorkshop: workshop id is 0.");
        return false;
    }

    if (auto queued = _conVars.ExecuteServerCommand(std::format("host_workshop_map {}", workshopId)); !queued)
    {
        Log::Warn("Map::ChangeToWorkshop: {}", queued.error().Detail);
        return false;
    }
    return true;
}

}  // namespace VoltMod
