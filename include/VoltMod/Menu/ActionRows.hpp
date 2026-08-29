#pragma once

#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Pawn.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The admin-panel rows: one admin, one target, and the services a row press runs through.
 *
 * Each method returns a @ref MenuItem, so these mix with the plain specs in one builder chain:
 *
 * @code
 * ActionRows rows({.Actions = app.Actions, .Policy = runtime.Policy, .Translations = runtime.Translations,
 *                  .Players = runtime.Players, .Entities = runtime.Entities, .Menus = runtime.Menus,
 *                  .Effects = &app.Effects},
 *                 adminRef, targetRef);
 *
 * MenuBuilder(title)
 *     .Add(rows.Action("action.kill", Actions::Kill))
 *     .Add(rows.StateToggle("action.freeze", InMoveType(MoveType::None), Actions::Freeze))
 *     .Add(rows.Presets({.LabelKey = "action.health", .Unit = "HP", .Presets = HealthPresets,
 *                        .Action = Actions::SetHealth}))
 *     .Add(rows.Effect(Effects::Ghost))
 *     .Build();
 * @endcode
 *
 * Labels are translation keys resolved in the admin's language. A row's enabled state is one
 * @ref Policy::Authorize call taken when the row is built, and the same check runs again through
 * the dispatcher when the row is pressed - against the two @ref PlayerRef values, not whoever
 * occupies their slots by then, so a departed admin or a reused target slot is refused rather
 * than retargeted.
 */
class ActionRows
{
public:
    /** Everything a row press reaches. All must outlive the rows, which one Load/Unload cycle
     *  guarantees; @ref Effects may be null for a panel with no effect rows. */
    struct Services
    {
        ActionDispatcher& Actions;
        Policy& Policy;
        Translations& Translations;
        PlayerManager& Players;
        EntitySystem& Entities;
        MenuSession& Menus;
        EffectManager* Effects = nullptr;
    };

    /** @p target is empty for a panel with no target yet; its rows then deny, because
     *  @ref Policy::Authorize is asked about a reference that names nobody. */
    ActionRows(const Services& services, PlayerRef admin, std::optional<PlayerRef> target);

    /** Whether @p permission is granted for this admin/target pair right now. */
    [[nodiscard]] bool Allowed(std::string_view permission) const;

    /** @p key in the admin's language, with `{token}` substitutions. */
    [[nodiscard]] std::string Tr(std::string_view key, Tokens tokens = {}) const;

    /** A button row running a single-target @ref VoltMod::Action against the pair. */
    [[nodiscard]] MenuItem Action(std::string_view labelKey, const VoltMod::Action& action);

    /**
     * A toggle row whose state is @p isActive over the target's pawn, re-read on every redraw,
     * and whose flip runs @p action. Predicates live in Entities/PawnPredicates.hpp.
     */
    [[nodiscard]] MenuItem StateToggle(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                                       const VoltMod::Action& action);

    /** A choice row over a fixed list of numbers. @ref Unit is appended to each label. */
    struct PresetSpec
    {
        std::string_view LabelKey;
        /** Appended to every preset: `"100 HP"`. */
        std::string_view Unit;
        std::span<const int> Presets;
        const ParamAction& Action;
        /** Which preset the row starts on. */
        int Index = 0;
    };

    /** A/D picks a preset and E applies it. The menu stays open, so a value can be tried,
     *  adjusted and applied again without reopening the panel. */
    [[nodiscard]] MenuItem Presets(const PresetSpec& spec);

    /** An on/off row for a data-defined effect, read from and written through the
     *  @ref EffectManager in @ref Services::Effects. Inert when that is null. */
    [[nodiscard]] MenuItem Effect(const EffectDescriptor& effect);

    /** A submenu over @ref EffectDescriptor::Choices, with a reset row when `ResetLabelKey` is
     *  set. Picking a choice applies it and closes the panel. */
    [[nodiscard]] MenuItem EffectPicker(const EffectDescriptor& effect);

private:
    /** The picker @ref EffectPicker opens, built when the row is pressed so it names whoever the
     *  target is by then. Null when the target has left. */
    std::shared_ptr<Menu> BuildPicker(const EffectDescriptor& effect, bool allowed) const;

    /** The target as a dispatcher takes it: an absent one is a reference naming nobody, which
     *  @ref Policy::Authorize refuses. */
    [[nodiscard]] PlayerRef TargetRef() const { return _target.value_or(PlayerRef{}); }

    /** By value, so the rows a builder chain produces do not depend on the caller's spec
     *  outliving them. The references inside it still must. */
    Services _services;
    PlayerRef _admin;
    std::optional<PlayerRef> _target;
};

}  // namespace VoltMod
