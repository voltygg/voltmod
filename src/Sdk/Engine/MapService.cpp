#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Sdk/Engine/ConVarService.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/MapService.hpp>
#include <eiface.h>
#include <format>

namespace VoltMod::Sdk
{

using namespace VoltMod::Core;

MapService::MapService(GameInterfaces& interfaces, ConVarService& conVars) : _interfaces(interfaces), _conVars(conVars)
{}

bool MapService::IsValid(const char* name) const
{
    if (!name || !*name)
        return false;

    auto* engine = _interfaces.Engine;
    if (!engine)
    {
        Log::Warn("MapService::IsValid: IVEngineServer2 not available.");
        return false;
    }

    return engine->IsMapValid(name) != 0;
}

bool MapService::ChangeLevel(const char* name)
{
    if (!IsValid(name))
    {
        Log::Warn("MapService::ChangeLevel: '{}' is not a loadable map.", name ? name : "");
        return false;
    }

    // s2 is the landmark argument, which only save-game transitions use.
    _interfaces.Engine->ChangeLevel(name, nullptr);
    return true;
}

bool MapService::ChangeToWorkshop(uint64_t workshopId)
{
    if (workshopId == 0)
    {
        Log::Warn("MapService::ChangeToWorkshop: workshop id is 0.");
        return false;
    }

    _conVars.ExecuteServerCommand(std::format("host_workshop_map {}", workshopId).c_str());
    return true;
}

}  // namespace VoltMod::Sdk
