#include <VoltMod/Core/EffectManager.hpp>
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
// their address in row lambdas is safe for the process lifetime. Every row captures the
// context's runtime pointer, which is null only for a context nobody bound - in which case
// MenuContext::Allowed has already disabled the row.

MenuBuilder& MenuBuilder::AddActionRow(std::string_view labelKey, const Players::Action& action)
{
    const Players::Action* a = &action;
    return AddButton(
        _context.Tr(labelKey),
        [rt = _context.Rt, admin = _context.Admin, target = _context.Target, a](int) {
            if (rt)
                ActionDispatcher{*rt}.Run(admin, target, *a);
        },
        _context.Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::AddStateToggleRow(std::string_view labelKey,
                                            std::function<bool(const Sdk::PlayerController&)> isActive,
                                            const Players::Action& action)
{
    const Players::Action* a = &action;
    Runtime* rt = _context.Rt;
    int target = _context.Target;
    return AddToggle(
        _context.Tr(labelKey), _context.Tr("effectState.on"), _context.Tr("effectState.off"),
        [rt, target, isActive = std::move(isActive)](int) {
            if (!rt)
                return false;
            Sdk::PlayerController pc = rt->Entities.Controller(target);
            return pc.IsValid() && isActive(pc);
        },
        [rt, admin = _context.Admin, target, a](int) {
            if (rt)
                ActionDispatcher{*rt}.Run(admin, target, *a);
        },
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
        [rt = _context.Rt, admin = _context.Admin, target = _context.Target, a](int slot, const int& value) {
            if (!rt)
                return;
            ActionDispatcher{*rt}.Run(admin, target, value, *a);
            rt->Menus.CloseAllMenus(slot);
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
        [rt = _context.Rt, effects, admin = _context.Admin, target, e](int) {
            if (rt && effects)
                Players::ToggleEffect(*rt, *effects, admin, target, *e);
        },
        _context.Allowed(effect.Permission));
}

namespace
{

/** Picker submenu for a ParamEffectDescriptor: one button per choice plus an optional reset row. */
std::shared_ptr<MenuView> BuildEffectPicker(MenuContext ctx, const Players::ParamEffectDescriptor& effect)
{
    if (!ctx.Rt)
        return nullptr;

    auto* target = ctx.Rt->Players.GetPlayerBySlot(ctx.Target);
    if (!target)
        return nullptr;

    bool allowed = ctx.Allowed(effect.Permission);
    const Players::ParamEffectDescriptor* e = &effect;
    Core::EffectManager* effects = ctx.Effects;
    Runtime* rt = ctx.Rt;
    MenuManager* menus = &ctx.Rt->Menus;
    MenuBuilder builder(std::format("{}: {}", ctx.Tr(effect.NameKey), target->GetName()));

    auto choices = effect.Choices ? effect.Choices() : std::vector<Players::EffectChoice>{};
    for (const auto& choice : choices)
    {
        int param = choice.Param;
        builder.AddButton(
            choice.Label,
            [rt, effects, menus, admin = ctx.Admin, targetSlot = ctx.Target, e, param](int slot) {
                if (effects)
                    Players::ApplyEffect(*rt, *effects, admin, targetSlot, param, *e);
                menus->CloseAllMenus(slot);
            },
            allowed);
    }

    if (!effect.ResetLabelKey.empty())
    {
        builder.AddButton(
            ctx.Tr(effect.ResetLabelKey),
            [rt, effects, menus, admin = ctx.Admin, targetSlot = ctx.Target, e](int slot) {
                if (effects)
                    Players::ClearEffect(*rt, *effects, admin, targetSlot, *e);
                menus->CloseAllMenus(slot);
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
