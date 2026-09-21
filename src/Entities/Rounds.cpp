#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/Rounds.hpp>
#include <VoltMod/Schema/Generated/CCSGameRulesProxy.hpp>

namespace VoltMod
{

Status Rounds::Available() const
{
    if (!_bindings.TerminateRound)
        return std::unexpected(Error::Unsupported("the CCSGameRules::TerminateRound signature did not bind"));
    return {};
}

Status Rounds::End(RoundEndReason reason, float delaySeconds) const
{
    if (Status available = Available(); !available)
        return available;

    // The game rules live outside the entity system; their proxy entity holds the pointer.
    const Entity proxy = _entities.FindByClassName({}, "cs_gamerules");
    void* rules = proxy ? Schema::CCSGameRulesProxy{proxy.Raw()}.GameRules() : nullptr;
    if (!rules)
        return std::unexpected(Error::NotReady("no game rules: is a map running?"));

    _bindings.TerminateRound(rules, delaySeconds, static_cast<uint32_t>(reason), nullptr);
    return {};
}

}  // namespace VoltMod
