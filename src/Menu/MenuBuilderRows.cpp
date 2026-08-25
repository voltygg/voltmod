#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>
#include <format>

namespace VoltMod::Menu
{

using Players::ActionDispatcher;

// Context-aware rows. Descriptors are namespace-scope globals in the consumer, so capturing
// their address in row lambdas is safe for the process lifetime.

MenuBuilder& MenuBuilder::AddActionRow(std::string_view labelKey, const Players::Action& action)
{
    const Players::Action* a = &action;
    return AddButton(
        _context.Tr(labelKey),
        [admin = _context.Admin, target = _context.Target, a](int) { ActionDispatcher{}.Run(admin, target, *a); },
        _context.Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::AddStateToggleRow(std::string_view labelKey,
                                            std::function<bool(const Sdk::PlayerController&)> isActive,
                                            const Players::Action& action)
{
    const Players::Action* a = &action;
    int target = _context.Target;
    return AddToggle(
        _context.Tr(labelKey), _context.Tr("effectState.on"), _context.Tr("effectState.off"),
        [target, isActive = std::move(isActive)](int) {
            Sdk::PlayerController pc = VoltMod::Detail::Rt().Entities.Controller(target);
            return pc.IsValid() && isActive(pc);
        },
        [admin = _context.Admin, target, a](int) { ActionDispatcher{}.Run(admin, target, *a); },
        _context.Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::AddPresetChoiceRow(std::string_view labelKey, std::string_view unit,
                                             std::span<const int> presets, const Players::ParamAction& action,
                                             int initialIndex)
{
    std::vector<ChoiceOption<int>::Choice> choices;
    choices.reserve(presets.size());
    for (int value : presets)
        choices.push_back({std::format("{} {}", value, unit), value});

    const Players::ParamAction* a = &action;
    return AddChoice<int>(
        _context.Tr(labelKey), std::move(choices),
        [admin = _context.Admin, target = _context.Target, a](int slot, const int& value) {
            ActionDispatcher{}.Run(admin, target, value, *a);
            VoltMod::Detail::Rt().Menus.CloseAllMenus(slot);
        },
        _context.Allowed(action.Permission), initialIndex);
}

MenuBuilder& MenuBuilder::AddEffectToggleRow(const Players::EffectDescriptor& effect)
{
    const Players::EffectDescriptor* e = &effect;
    Core::EffectManager* effects = _context.Effects;
    int target = _context.Target;
    return AddToggle(
        _context.Tr(effect.NameKey), _context.Tr("effectState.on"), _context.Tr("effectState.off"),
        [effects, target, id = effect.Id](int) { return effects && effects->IsActive(target, id); },
        [effects, admin = _context.Admin, target, e](int) {
            if (effects)
                Players::ToggleEffect(*effects, admin, target, *e);
        },
        _context.Allowed(effect.Permission));
}

namespace
{

/** Picker submenu for a ParamEffectDescriptor: one button per choice plus an optional reset row. */
std::shared_ptr<MenuView> BuildEffectPicker(MenuContext ctx, const Players::ParamEffectDescriptor& effect)
{
    auto* target = VoltMod::Detail::Rt().Players.GetPlayerBySlot(ctx.Target);
    if (!target)
        return nullptr;

    bool allowed = ctx.Allowed(effect.Permission);
    const Players::ParamEffectDescriptor* e = &effect;
    Core::EffectManager* effects = ctx.Effects;
    MenuBuilder builder(std::format("{}: {}", ctx.Tr(effect.NameKey), target->GetName()));

    auto choices = effect.Choices ? effect.Choices() : std::vector<Players::EffectChoice>{};
    for (const auto& choice : choices)
    {
        int param = choice.Param;
        builder.AddButton(
            choice.Label,
            [effects, admin = ctx.Admin, targetSlot = ctx.Target, e, param](int slot) {
                if (effects)
                    Players::ApplyEffect(*effects, admin, targetSlot, param, *e);
                VoltMod::Detail::Rt().Menus.CloseAllMenus(slot);
            },
            allowed);
    }

    if (!effect.ResetLabelKey.empty())
    {
        builder.AddButton(
            ctx.Tr(effect.ResetLabelKey),
            [effects, admin = ctx.Admin, targetSlot = ctx.Target, e](int slot) {
                if (effects)
                    Players::ClearEffect(*effects, admin, targetSlot, *e);
                VoltMod::Detail::Rt().Menus.CloseAllMenus(slot);
            },
            allowed);
    }

    return builder.Build();
}

}  // namespace

MenuBuilder& MenuBuilder::AddEffectPickerRow(const Players::ParamEffectDescriptor& effect)
{
    const Players::ParamEffectDescriptor* e = &effect;
    return AddSubmenu(
        _context.Tr(effect.NameKey), [ctx = _context, e](int) { return BuildEffectPicker(ctx, *e); },
        _context.Allowed(effect.Permission));
}

}  // namespace VoltMod::Menu
