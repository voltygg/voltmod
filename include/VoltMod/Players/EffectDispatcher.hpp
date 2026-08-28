#pragma once

#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>

namespace VoltMod
{

/**
 * @brief Runs data-defined effects (@ref EffectDescriptor) against a player, the effect-side
 * counterpart of @ref ActionDispatcher.
 *
 * Every verb resolves the admin/target pair through the @ref ActionDispatcher it wraps, so
 * `Policy::Authorize` supplies the permission check, the targetability check, and the broadcast
 * callback. The @ref EffectManager it drives is plugin-owned, so a plugin holds the dispatcher next to
 * its manager (`EffectDispatcher PlayerEffects{Actions, Effects};`) rather than reaching for a
 * runtime member.
 */
class EffectDispatcher
{
public:
    /** @p actions supplies the roster, the controllers and the policy (already wired for the
     *  plugin's other single-target dispatch); @p effects owns the per-slot effect state. Both
     *  must outlive the dispatcher. Cheap to construct, so a call site may build one per dispatch
     *  or hold one as a long-lived member. */
    EffectDispatcher(ActionDispatcher& actions, EffectManager& effects) : _actions(actions), _effects(effects) {}

    EffectDispatcher(const EffectDispatcher&) = delete;
    EffectDispatcher& operator=(const EffectDispatcher&) = delete;

    /** Apply if inactive, clear if active. Broadcasts OnKey/OffKey. The default menu-row verb.
     *  @p param is forwarded to `Setup` (0 for a plain toggle). */
    void Toggle(PlayerRef admin, PlayerRef target, const EffectDescriptor& effect, int param = 0) const;
    /** (Re)apply unconditionally, broadcasting OnKey. */
    void Apply(PlayerRef admin, PlayerRef target, const EffectDescriptor& effect, int param = 0) const;
    /** Cancel if active, broadcasting OffKey (when set). */
    void Clear(PlayerRef admin, PlayerRef target, const EffectDescriptor& effect) const;

private:
    ActionDispatcher& _actions;
    EffectManager& _effects;
};

}  // namespace VoltMod
