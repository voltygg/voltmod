#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Menu/Html/HtmlMenuManager.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <format>

namespace VoltMod
{

// Context-aware rows. `_menus` is null only when nobody bound a HtmlMenuManager (the plain
// constructor) - Allowed() already turns that into a disabled row, so the inert fallbacks below
// build a harmless disabled row of the matching shape rather than dereferencing a null manager.

MenuBuilder& MenuBuilder::Row(std::string_view labelKey, const Action& action)
{
    if (!_menus)
        return Button(std::string(labelKey), [](int) {}, false);

    ActionDispatcher* actions = &_menus->Actions();
    PlayerRef admin = _admin;
    PlayerRef target = _target.value_or(PlayerRef{});
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
    PlayerRef admin = _admin;
    PlayerRef target = _target.value_or(PlayerRef{});
    const Action* a = &action;
    return Toggle(
        Tr(labelKey), Tr("effectState.on"), Tr("effectState.off"),
        [entities, target, isActive = std::move(isActive)](int) {
            Pawn pawn = entities->PawnOf(target.Slot);
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
    HtmlMenuManager* menus = _menus;
    PlayerRef admin = _admin;
    PlayerRef target = _target.value_or(PlayerRef{});
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
    PlayerRef admin = _admin;
    PlayerRef target = _target.value_or(PlayerRef{});
    return Toggle(
        Tr(effect.NameKey), Tr("effectState.on"), Tr("effectState.off"),
        [effects, target, id = effect.Id](int) { return effects && effects->IsActive(target.Slot, id); },
        [actions, effects, admin, target, e](int) {
            if (effects)
                EffectDispatcher{*actions, *effects}.Toggle(admin, target, *e);
        },
        Allowed(effect.Permission));
}

std::shared_ptr<MenuView> MenuBuilder::BuildEffectPicker(HtmlMenuManager& menus, PlayerRef admin, PlayerRef target,
                                                         EffectManager* effects, bool allowed,
                                                         const EffectDescriptor& effect)
{
    // Get(PlayerRef), not Get(slot): a picker built for a player who has since left must not
    // reopen against whoever took the slot.
    auto* targetPlayer = menus.Players().Get(target);
    if (!targetPlayer)
        return nullptr;

    ActionDispatcher* actions = &menus.Actions();
    HtmlMenuManager* menusPtr = &menus;
    Translations& tr = menus.Translation();
    const EffectDescriptor* e = &effect;

    MenuBuilder builder(std::format("{}: {}", tr.Get(effect.NameKey, admin.Slot), targetPlayer->Name()));

    auto choices = effect.Choices ? effect.Choices() : std::vector<EffectChoice>{};
    for (const auto& choice : choices)
    {
        int param = choice.Param;
        builder.Button(
            choice.Label,
            [actions, effects, menusPtr, admin, target, e, param](int slot) {
                if (effects)
                    EffectDispatcher{*actions, *effects}.Apply(admin, target, *e, param);
                menusPtr->CloseAllMenus(slot);
            },
            allowed);
    }

    if (!effect.ResetLabelKey.empty())
    {
        builder.Button(
            tr.Get(effect.ResetLabelKey, admin.Slot),
            [actions, effects, menusPtr, admin, target, e](int slot) {
                if (effects)
                    EffectDispatcher{*actions, *effects}.Clear(admin, target, *e);
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

    HtmlMenuManager* menus = _menus;
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
            return MenuBuilder::BuildEffectPicker(*menus, admin, *target, effects, allowed, *e);
        },
        allowed);
}

}  // namespace VoltMod
