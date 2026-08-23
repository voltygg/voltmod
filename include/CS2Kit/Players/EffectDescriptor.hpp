#pragma once

#include <CS2Kit/Players/ActionDispatcher.hpp>
#include <functional>
#include <string>
#include <vector>

namespace CS2Kit::Core
{
class EffectManager;  // Core/EffectManager.hpp; by reference only, so a declaration is enough
}

namespace CS2Kit::Players
{

/** Lifetime policy, declared as data on the descriptor (not baked into the body). */
enum class EffectScope
{
    Persistent, /**< Lives until toggled off, death, disconnect, or unload. */
    Round,      /**< Also auto-cancels on round end/prestart. */
    Session     /**< Survives death; cleared only on toggle-off, disconnect, or unload. */
};

/**
 * @brief What an effect's setup body hands back: the two closures @ref EffectManager drives.
 * `OnTick` runs every `TickIntervalMs` (null for state-only effects); `OnStop` undoes whatever
 * was applied and runs exactly once when the effect ends for any reason.
 */
struct EffectInstance
{
    std::function<void()> OnTick;
    std::function<void()> OnStop;
};

/** One selectable option for a @ref ParamEffectDescriptor submenu. */
struct EffectChoice
{
    std::string Label;
    int Param;
};

/**
 * @brief A toggle / one-shot / timed player effect expressed as data, mirroring @ref Players::Action.
 *
 * `Setup` receives the resolved, permission/immunity-checked context, applies the effect, and
 * returns its @ref EffectInstance. Lifetime is declarative: `Scope`, `TickIntervalMs`, and
 * `DurationMs` are forwarded to @ref EffectManager. An empty `OnKey`/`OffKey` suppresses that
 * broadcast. Dispatch via @ref ToggleEffect / @ref ApplyEffect / @ref ClearEffect, which apply
 * `runtime.Policy` before running the body.
 */
struct EffectDescriptor
{
    std::string Permission; /**< Consumer-defined permission token; "" skips the check. */
    int Id;                 /**< Plugin-defined key into the per-slot Core::EffectManager map. */
    std::string NameKey;    /**< Translation key for the menu row label. */
    std::string OnKey;      /**< Broadcast key when applied ("" = silent). */
    std::string OffKey;     /**< Broadcast key when cleared ("" = silent). */
    EffectScope Scope = EffectScope::Persistent;
    int TickIntervalMs = 0;
    int DurationMs = 0;
    bool RequireAlive = false;
    std::function<EffectInstance(const Players::ActionContext&)> Setup;
};

/** Like @ref EffectDescriptor but `Setup` receives a menu-supplied int (e.g. a model index). */
struct ParamEffectDescriptor
{
    std::string Permission;
    int Id;
    std::string NameKey;
    std::string OnKey;
    std::string OffKey;
    std::string ResetLabelKey; /**< "" = no reset row in the picker. */
    EffectScope Scope = EffectScope::Persistent;
    int TickIntervalMs = 0;
    int DurationMs = 0;
    bool RequireAlive = false;
    std::function<std::vector<EffectChoice>()> Choices;
    std::function<EffectInstance(const Players::ActionContext&, int param)> Setup;
};

/** Apply if inactive, clear if active. Broadcasts OnKey/OffKey. The default menu-row verb. */
void ToggleEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const EffectDescriptor& effect);
/** (Re)apply unconditionally, broadcasting OnKey. */
void ApplyEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const EffectDescriptor& effect);
/** Cancel if active, broadcasting OffKey (when set). */
void ClearEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const EffectDescriptor& effect);

/** Apply the parameterized effect at `param`, broadcasting OnKey. */
void ApplyEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, int param,
                 const ParamEffectDescriptor& effect);
/** Cancel the parameterized effect if active, broadcasting OffKey. */
void ClearEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const ParamEffectDescriptor& effect);

}  // namespace CS2Kit::Players
