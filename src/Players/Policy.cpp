#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Players/Policy.hpp>

namespace VoltMod
{

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

    if (!permission.empty())
    {
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

        if (!HasPermission(callerPlayer->SteamId(), permission))
            return std::unexpected(Error::Denied("cmd.noPermission"));
    }

    // Self-targeting is the framework's rule, not the plugin's: an immunity comparison has
    // nothing sensible to say about a player and themselves, and every gate used to answer it
    // differently.
    if (targetPlayer && targetPlayer != callerPlayer && CanTarget && !CanTarget(*callerPlayer, *targetPlayer))
        return std::unexpected(Error::Immune("target.immune"));

    return Authorized{.Caller = *callerPlayer, .Target = targetPlayer};
}

}  // namespace VoltMod
