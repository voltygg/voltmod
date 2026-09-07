#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Players/Policy.hpp>

namespace VoltMod
{

Status Policy::CheckPermission(const Player& caller, std::string_view permission) const
{
    if (permission.empty())
        return {};

    // Without a policy there is no trusted permission source. Deny and say so once, so the
    // plugin misconfiguration is visible instead of silently locking every admin out.
    if (!HasPermission)
    {
        if (!_missingPermissionWarned)
        {
            _missingPermissionWarned = true;
            Log::Error(
                "Denying '{}': no HasPermission policy is installed. "
                "Set Runtime::Policy.HasPermission in OnLoad.",
                permission);
        }
        return std::unexpected(Error::Denied("cmd.noPermission"));
    }

    if (!HasPermission(caller.SteamId(), permission))
        return std::unexpected(Error::Denied("cmd.noPermission"));

    return {};
}

Status Policy::CheckImmunity(const Player& caller, int64_t targetSteamId) const
{
    if (targetSteamId == caller.SteamId() || !CanTarget)
        return {};

    if (!CanTarget(caller.SteamId(), targetSteamId))
        return std::unexpected(Error::Immune("target.immune"));

    return {};
}

Result<Authorized> Policy::Authorize(PlayerRef caller, std::optional<PlayerRef> target,
                                     std::string_view permission) const
{
    Player* callerPlayer = _players.Get(caller);
    if (!callerPlayer)
        return std::unexpected(Error::NotFound("caller is not connected"));

    Player* targetPlayer = nullptr;
    if (target)
    {
        targetPlayer = _players.Get(*target);
        if (!targetPlayer)
            return std::unexpected(Error{ErrorCode::NotFound, "target is not connected", "target.noMatch"});
    }

    if (auto allowed = CheckPermission(*callerPlayer, permission); !allowed)
        return std::unexpected(allowed.error());

    if (targetPlayer)
        if (auto allowed = CheckImmunity(*callerPlayer, targetPlayer->SteamId()); !allowed)
            return std::unexpected(allowed.error());

    return Authorized{.Caller = *callerPlayer, .Target = targetPlayer};
}

Status Policy::AuthorizeSteamId(PlayerRef caller, int64_t targetSteamId, std::string_view permission) const
{
    Player* callerPlayer = _players.Get(caller);
    if (!callerPlayer)
        return std::unexpected(Error::NotFound("caller is not connected"));

    if (auto allowed = CheckPermission(*callerPlayer, permission); !allowed)
        return std::unexpected(allowed.error());

    return CheckImmunity(*callerPlayer, targetSteamId);
}

}  // namespace VoltMod
