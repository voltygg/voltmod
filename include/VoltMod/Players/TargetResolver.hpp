#pragma once

#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/Targeting.hpp>
#include <VoltMod/Runtime.hpp>
#include <expected>
#include <functional>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Targetability policy (immunity, same-team rules, ...). Empty means "use `runtime.Policy.CanTarget`". */
using CanTargetFn = std::function<bool(Player& caller, Player& target)>;

/**
 * Resolve a target token against the connected players.
 *
 * Grammar and rule semantics live in @ref Targeting.hpp (`@all`, `@me`, `@t`, `@random`,
 * `#slot`, SteamID forms, name fragments, ...). The returned players honor @p rules -
 * a single-target command (`AllowMultiple == false`) gets exactly one player or a
 * @ref TargetFailure explaining what to tell the caller.
 *
 * @p runtime supplies the roster, the controllers, and the default policy; include
 * <VoltMod/Runtime.hpp> at the call site.
 * @p caller null means the server console: always allowed, `@me` never matches.
 * @p canTarget overrides the immunity policy; empty uses `runtime.Policy.CanTarget`.
 */
std::expected<std::vector<Player*>, TargetFailure> ResolveTargets(Runtime& runtime, std::string_view token,
                                                                  Player* caller, const TargetRules& rules = {},
                                                                  const CanTargetFn& canTarget = {});

}  // namespace VoltMod
