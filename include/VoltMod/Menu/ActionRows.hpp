#pragma once

#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Pawn.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
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

/** Builds common admin actions for one admin and optional target. */
class ActionRows
{
public:
    /** Referenced objects must outlive the generated rows. Effects may be null. */
    struct Services
    {
        ActionDispatcher& Actions;
        Policy& Policy;
        Translations& Translations;
        PlayerManager& Players;
        EntitySystem& Entities;
        MenuSurface& Menus;
        EffectManager* Effects = nullptr;
    };

    /** Rows requiring a target are disabled when @p target is empty. */
    ActionRows(const Services& services, PlayerRef admin, std::optional<PlayerRef> target);

    /** Checks the permission each time the row is drawn or used. */
    [[nodiscard]] EnabledCondition Allows(std::string_view permission) const;

    /** Translates @p key for the admin. */
    [[nodiscard]] std::string Tr(std::string_view key, Tokens tokens = {}) const;

    /** A button that runs a single-target action. */
    [[nodiscard]] MenuItem Action(std::string_view labelKey, const VoltMod::Action& action);

    /**
     * A toggle row whose state is @p isActive over the target's pawn, re-read on every redraw,
     * and whose flip runs @p action. Predicates live in Entities/PawnPredicates.hpp.
     */
    [[nodiscard]] MenuItem StateToggle(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                                       const VoltMod::Action& action);

    /** A choice row over a fixed list of numbers. */
    struct PresetSpec
    {
        std::string_view LabelKey;
        /** Appended to each preset, for example `"100 HP"`. */
        std::string_view Unit;
        std::span<const int> Presets;
        const ParamAction& Action;
        /** Which preset the row starts on. */
        int Index = 0;
    };

    /** Applies the selected preset after stepping stops. */
    [[nodiscard]] MenuItem Presets(const PresetSpec& spec);

    /** An on/off row for a data-defined effect. */
    [[nodiscard]] MenuItem Effect(const EffectDescriptor& effect);

    /** A submenu over an effect's choices. */
    [[nodiscard]] MenuItem EffectPicker(const EffectDescriptor& effect);

private:
    std::shared_ptr<Menu> BuildPicker(const EffectDescriptor& effect, EnabledCondition allowed) const;

    [[nodiscard]] EffectDispatcher Effects() const;

    [[nodiscard]] EnabledCondition EffectAllows(const EffectDescriptor& effect) const;

    [[nodiscard]] PlayerRef TargetRef() const { return _target.value_or(PlayerRef{}); }

    /** Shared by row callbacks so each menu stores one copy. The referenced services must still
     *  outlive the rows. */
    std::shared_ptr<const Services> _services;
    PlayerRef _admin;
    std::optional<PlayerRef> _target;
};

}  // namespace VoltMod
