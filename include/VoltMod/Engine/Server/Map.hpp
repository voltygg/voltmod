#pragma once

#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Engine/ConVars/ConVar.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Map validation and level changes through IVEngineServer2. */
class Map
{
public:
    /** Both dependencies must outlive this service. */
    Map(Interfaces& interfaces, ConVars& conVars);

    /** A map started, with its name. Raised after the framework's own services have refreshed. */
    Event<std::string_view> Started;

    /** Whether the engine can load a mounted, non-workshop map. */
    bool IsValid(std::string_view name) const;

    /** Validate and queue a non-workshop map change. */
    bool ChangeLevel(std::string_view name);

    /** Queue `host_workshop_map`. Returns false for zero or when queuing fails. */
    bool ChangeToWorkshop(uint64_t workshopId);

    /** The map being played: the StartupServer name, or the engine globals' `mapname` after a late
     *  load. Empty when neither is available. */
    std::string Current() const;

    /** Framework hook entry point. Plugins read @ref Current. */
    void SetCurrent(std::string name) { _current = std::move(name); }

private:
    Interfaces& _interfaces;
    ConVars& _conVars;
    std::string _current;
};

}  // namespace VoltMod
