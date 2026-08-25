#pragma once

#include <VoltMod/Players/EffectDescriptor.hpp>

namespace VoltMod
{
class Runtime;
}

namespace VoltMod::Core
{
class EffectManager;  // Core/EffectManager.hpp; by reference only, so a declaration is enough
}

namespace VoltMod::Players
{

/**
 * @brief Runs data-defined effects (@ref EffectDescriptor, @ref ParamEffectDescriptor) against a
 * player, the effect-side counterpart of @ref ActionDispatcher.
 *
 * Every verb resolves the admin/target pair through an @ref ActionDispatcher first, so
 * `runtime.Policy` supplies the permission check, the targetability check, and the broadcast sink.
 * The @ref Core::EffectManager it drives is plugin-owned, so a plugin holds the dispatcher next to
 * its manager (`EffectDispatcher PlayerEffects{runtime, effects};`) rather than reaching for a
 * runtime member.
 */
class EffectDispatcher
{
public:
    /** @p runtime supplies the roster, the controllers and the policy; @p effects owns the per-slot
     *  effect state. Both must outlive the dispatcher. Cheap to construct, so a call site may build
     *  one per dispatch. */
    EffectDispatcher(Runtime& runtime, Core::EffectManager& effects) : _actions(runtime), _effects(effects) {}

    EffectDispatcher(const EffectDispatcher&) = delete;
    EffectDispatcher& operator=(const EffectDispatcher&) = delete;

    /** Apply if inactive, clear if active. Broadcasts OnKey/OffKey. The default menu-row verb. */
    void Toggle(int adminSlot, int targetSlot, const EffectDescriptor& effect) const;
    /** (Re)apply unconditionally, broadcasting OnKey. */
    void Apply(int adminSlot, int targetSlot, const EffectDescriptor& effect) const;
    /** Cancel if active, broadcasting OffKey (when set). */
    void Clear(int adminSlot, int targetSlot, const EffectDescriptor& effect) const;

    /** Apply the parameterized effect at @p param, broadcasting OnKey. */
    void Apply(int adminSlot, int targetSlot, int param, const ParamEffectDescriptor& effect) const;
    /** Cancel the parameterized effect if active, broadcasting OffKey. */
    void Clear(int adminSlot, int targetSlot, const ParamEffectDescriptor& effect) const;

private:
    /** Shared body for the Clear verbs (both key off Permission/Id/OffKey only). */
    void ClearById(int adminSlot, int targetSlot, const std::string& permission, int id,
                   const std::string& offKey) const;
    void BroadcastKey(const ActionContext& ctx, const std::string& key) const;

    ActionDispatcher _actions;
    Core::EffectManager& _effects;
};

}  // namespace VoltMod::Players
