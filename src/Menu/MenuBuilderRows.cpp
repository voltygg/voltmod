#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <format>

namespace VoltMod
{

// Context-aware rows. Descriptors are namespace-scope globals in the consumer, so capturing
// their address in row lambdas is safe for the process lifetime. A null MenuContext::Rt means
// nobody bound the context, which Allowed() already turns into a disabled row - and MenuManager
// activates only enabled rows, so only the render-time predicates below tolerate it.

MenuBuilder& MenuBuilder::AddActionRow(std::string_view labelKey, const Action& action)
{
    const Action* a = &action;
    return AddButton(
        _context.Tr(labelKey),
        [rt = _context.Rt, admin = _context.Admin, target = _context.Target, a](int) {
            ActionDispatcher{*rt}.Run(admin, target, *a);
        },
        _context.Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::AddStateToggleRow(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                                            const Action& action)
{
    const Action* a = &action;
    Runtime* rt = _context.Rt;
    int target = _context.Target;
    return AddToggle(
        _context.Tr(labelKey), _context.Tr("effectState.on"), _context.Tr("effectState.off"),
        [rt, target, isActive = std::move(isActive)](int) {
            if (!rt)
                return false;
            Pawn pawn = rt->Entities.PawnOf(target);
            return pawn && isActive(pawn);
        },
        [rt, admin = _context.Admin, target, a](int) { ActionDispatcher{*rt}.Run(admin, target, *a); },
        _context.Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::AddPresetChoiceRow(std::string_view labelKey, std::string_view unit,
                                             std::span<const int> presets, const ParamAction& action, int initialIndex)
{
    std::vector<ChoiceOption<int>::Choice> choices;
    choices.reserve(presets.size());
    for (int value : presets)
        choices.push_back({std::format("{} {}", value, unit), value});

    const ParamAction* a = &action;
    return AddChoice<int>(
        _context.Tr(labelKey), std::move(choices),
        [rt = _context.Rt, admin = _context.Admin, target = _context.Target, a](int slot, const int& value) {
            ActionDispatcher{*rt}.Run(admin, target, value, *a);
            rt->Menus.CloseAllMenus(slot);
        },
        _context.Allowed(action.Permission), initialIndex);
}

MenuBuilder& MenuBuilder::AddEffectToggleRow(const EffectDescriptor& effect)
{
    const EffectDescriptor* e = &effect;
    EffectManager* effects = _context.Effects;
    int target = _context.Target;
    return AddToggle(
        _context.Tr(effect.NameKey), _context.Tr("effectState.on"), _context.Tr("effectState.off"),
        [effects, target, id = effect.Id](int) { return effects && effects->IsActive(target, id); },
        [rt = _context.Rt, effects, admin = _context.Admin, target, e](int) {
            if (effects)
                EffectDispatcher{*rt, *effects}.Toggle(admin, target, *e);
        },
        _context.Allowed(effect.Permission));
}

/** Picker submenu for a ParamEffectDescriptor: one button per choice plus an optional reset row. */
static std::shared_ptr<MenuView> BuildEffectPicker(MenuContext ctx, const ParamEffectDescriptor& effect)
{
    auto* target = ctx.Rt->Players.Get(ctx.Target);
    if (!target)
        return nullptr;

    bool allowed = ctx.Allowed(effect.Permission);
    const ParamEffectDescriptor* e = &effect;
    EffectManager* effects = ctx.Effects;
    Runtime* rt = ctx.Rt;
    MenuManager* menus = &ctx.Rt->Menus;
    MenuBuilder builder(std::format("{}: {}", ctx.Tr(effect.NameKey), target->Name()));

    auto choices = effect.Choices ? effect.Choices() : std::vector<EffectChoice>{};
    for (const auto& choice : choices)
    {
        int param = choice.Param;
        builder.AddButton(
            choice.Label,
            [rt, effects, menus, admin = ctx.Admin, targetSlot = ctx.Target, e, param](int slot) {
                if (effects)
                    EffectDispatcher{*rt, *effects}.Apply(admin, targetSlot, param, *e);
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
                    EffectDispatcher{*rt, *effects}.Clear(admin, targetSlot, *e);
                menus->CloseAllMenus(slot);
            },
            allowed);
    }

    return builder.Build();
}

MenuBuilder& MenuBuilder::AddEffectPickerRow(const ParamEffectDescriptor& effect)
{
    const ParamEffectDescriptor* e = &effect;
    return AddSubmenu(
        _context.Tr(effect.NameKey), [ctx = _context, e](int) { return BuildEffectPicker(ctx, *e); },
        _context.Allowed(effect.Permission));
}

}  // namespace VoltMod
