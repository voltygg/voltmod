#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Map.hpp>
#include <eiface.h>
#include <format>

namespace VoltMod::Engine
{

using namespace VoltMod::Core;

Map::Map(Interfaces& interfaces, ConVars& conVars) : _interfaces(interfaces), _conVars(conVars) {}

bool Map::IsValid(const char* name) const
{
    if (!name || !*name)
        return false;

    auto* engine = _interfaces.Engine;
    if (!engine)
    {
        Log::Warn("Map::IsValid: IVEngineServer2 not available.");
        return false;
    }

    return engine->IsMapValid(name) != 0;
}

bool Map::ChangeLevel(const char* name)
{
    if (!IsValid(name))
    {
        Log::Warn("Map::ChangeLevel: '{}' is not a loadable map.", name ? name : "");
        return false;
    }

    // s2 is the landmark argument, which only save-game transitions use.
    _interfaces.Engine->ChangeLevel(name, nullptr);
    return true;
}

bool Map::ChangeToWorkshop(uint64_t workshopId)
{
    if (workshopId == 0)
    {
        Log::Warn("Map::ChangeToWorkshop: workshop id is 0.");
        return false;
    }

    _conVars.ExecuteServerCommand(std::format("host_workshop_map {}", workshopId));
    return true;
}

}  // namespace VoltMod::Engine
