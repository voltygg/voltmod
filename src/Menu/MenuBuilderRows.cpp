#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <format>

namespace VoltMod
{

// Context-aware rows. `_menus` is null only when nobody bound a MenuManager (the plain
// constructor) - Allowed() already turns that into a disabled row, so the inert fallbacks below
// build a harmless disabled row of the matching shape rather than dereferencing a null manager.

MenuBuilder& MenuBuilder::Row(std::string_view labelKey, const Action& action)
{
    if (!_menus)
        return Button(std::string(labelKey), [](int) {}, false);

    ActionDispatcher* actions = &_menus->Actions();
    int admin = _admin.Slot;
    int target = _target ? _target->Slot : -1;
    const Action* a = &action;
    return Button(
        Tr(labelKey), [actions, admin, target, a](int) { actions->Run(admin, target, *a); },
        Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::StateToggle(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                                      const Action& action)
{
    if (!_menus)
        return Toggle(std::string(labelKey), "", "", [](int) { return false; }, [](int) {}, false);

    EntitySystem* entities = &_menus->Entities();
    ActionDispatcher* actions = &_menus->Actions();
    int admin = _admin.Slot;
    int target = _target ? _target->Slot : -1;
    const Action* a = &action;
    return Toggle(
        Tr(labelKey), Tr("effectState.on"), Tr("effectState.off"),
        [entities, target, isActive = std::move(isActive)](int) {
            Pawn pawn = entities->PawnOf(target);
            return pawn && isActive(pawn);
        },
        [actions, admin, target, a](int) { actions->Run(admin, target, *a); }, Allowed(action.Permission));
}

MenuBuilder& MenuBuilder::Presets(std::string_view labelKey, std::string_view unit, std::span<const int> presets,
                                  const ParamAction& action, int initialIndex)
{
    std::vector<ChoiceOption<int>::Choice> choices;
    choices.reserve(presets.size());
    for (int value : presets)
        choices.push_back({std::format("{} {}", value, unit), value});

    if (!_menus)
        return Choice<int>(std::string(labelKey), std::move(choices), [](int, const int&) {}, false, initialIndex);

    ActionDispatcher* actions = &_menus->Actions();
    MenuManager* menus = _menus;
    int admin = _admin.Slot;
    int target = _target ? _target->Slot : -1;
    const ParamAction* a = &action;
    return Choice<int>(
        Tr(labelKey), std::move(choices),
        [actions, menus, admin, target, a](int slot, const int& value) {
            actions->Run(admin, target, value, *a);
            menus->CloseAllMenus(slot);
        },
        Allowed(action.Permission), initialIndex);
}

MenuBuilder& MenuBuilder::Effect(const EffectDescriptor& effect)
{
    if (!_menus)
        return Toggle(effect.NameKey, "", "", [](int) { return false; }, [](int) {}, false);

    const EffectDescriptor* e = &effect;
    EffectManager* effects = _effects;
    ActionDispatcher* actions = &_menus->Actions();
    int admin = _admin.Slot;
    int target = _target ? _target->Slot : -1;
    return Toggle(
        Tr(effect.NameKey), Tr("effectState.on"), Tr("effectState.off"),
        [effects, target, id = effect.Id](int) { return effects && effects->IsActive(target, id); },
        [actions, effects, admin, target, e](int) {
            if (effects)
                EffectDispatcher{*actions, *effects}.Toggle(admin, target, *e);
        },
        Allowed(effect.Permission));
}

/** Picker submenu for an EffectDescriptor with Choices set: one button per choice plus an
 *  optional reset row. */
static std::shared_ptr<MenuView> BuildEffectPicker(MenuManager& menus, PlayerRef admin, PlayerRef target,
                                                   EffectManager* effects, bool allowed, const EffectDescriptor& effect)
{
    auto* targetPlayer = menus.Players().Get(target.Slot);
    if (!targetPlayer)
        return nullptr;

    ActionDispatcher* actions = &menus.Actions();
    MenuManager* menusPtr = &menus;
    Translations& tr = menus.Translation();
    const EffectDescriptor* e = &effect;
    int adminSlot = admin.Slot;
    int targetSlot = target.Slot;

    MenuBuilder builder(std::format("{}: {}", tr.Get(effect.NameKey, adminSlot), targetPlayer->Name()));

    auto choices = effect.Choices ? effect.Choices() : std::vector<EffectChoice>{};
    for (const auto& choice : choices)
    {
        int param = choice.Param;
        builder.Button(
            choice.Label,
            [actions, effects, menusPtr, adminSlot, targetSlot, e, param](int slot) {
                if (effects)
                    EffectDispatcher{*actions, *effects}.Apply(adminSlot, targetSlot, *e, param);
                menusPtr->CloseAllMenus(slot);
            },
            allowed);
    }

    if (!effect.ResetLabelKey.empty())
    {
        builder.Button(
            tr.Get(effect.ResetLabelKey, adminSlot),
            [actions, effects, menusPtr, adminSlot, targetSlot, e](int slot) {
                if (effects)
                    EffectDispatcher{*actions, *effects}.Clear(adminSlot, targetSlot, *e);
                menusPtr->CloseAllMenus(slot);
            },
            allowed);
    }

    return builder.Build();
}

MenuBuilder& MenuBuilder::EffectPicker(const EffectDescriptor& effect)
{
    if (!_menus)
        return Submenu(effect.NameKey, [](int) -> std::shared_ptr<MenuView> { return nullptr; }, false);

    MenuManager* menus = _menus;
    PlayerRef admin = _admin;
    std::optional<PlayerRef> target = _target;
    EffectManager* effects = _effects;
    bool allowed = Allowed(effect.Permission);
    const EffectDescriptor* e = &effect;
    return Submenu(
        Tr(effect.NameKey),
        [menus, admin, target, effects, allowed, e](int) -> std::shared_ptr<MenuView> {
            if (!target)
                return nullptr;
            return BuildEffectPicker(*menus, admin, *target, effects, allowed, *e);
        },
        allowed);
}

}  // namespace VoltMod
