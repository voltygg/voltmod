#include <VoltMod/Menu/ActionRows.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

ActionRows::ActionRows(const Services& services, PlayerRef admin, std::optional<PlayerRef> target)
    : _services(services), _admin(admin), _target(std::move(target))
{}

bool ActionRows::Allowed(std::string_view permission) const
{
    return _services.Policy.Authorize(_admin, _target, permission).has_value();
}

std::string ActionRows::Tr(std::string_view key, Tokens tokens) const
{
    return _services.Translations.Get(key, _admin.Slot, tokens);
}

MenuItem ActionRows::Action(std::string_view labelKey, const VoltMod::Action& action)
{
    // Services and refs by value into the callback, the descriptor by pointer: an Action is
    // static plugin data, while the row may outlive the ActionRows that produced it.
    return ButtonRow{
        .Label = Tr(labelKey),
        .Activate = [services = _services, admin = _admin, target = TargetRef(),
                     act = &action](int) { services.Actions.Run(admin, target, *act); },
        .Enabled = Allowed(action.Permission),
    }
        .ToItem();
}

MenuItem ActionRows::StateToggle(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                                 const VoltMod::Action& action)
{
    return ToggleRow{
        .Label = Tr(labelKey),
        .On = Tr("effectState.on"),
        .Off = Tr("effectState.off"),
        .Get =
            [services = _services, target = TargetRef(), isActive = std::move(isActive)](int) {
                Pawn pawn = services.Entities.PawnOf(target.Slot);
                return pawn && isActive(pawn);
            },
        .Flip = [services = _services, admin = _admin, target = TargetRef(),
                 act = &action](int) { services.Actions.Run(admin, target, *act); },
        .Enabled = Allowed(action.Permission),
    }
        .ToItem();
}

MenuItem ActionRows::Presets(const PresetSpec& spec)
{
    std::vector<std::pair<std::string, int>> choices;
    choices.reserve(spec.Presets.size());
    for (int value : spec.Presets)
        choices.emplace_back(std::format("{} {}", value, spec.Unit), value);

    return ChoiceRow<int>{
        .Label = Tr(spec.LabelKey),
        .Choices = std::move(choices),
        // The menu stays open: a preset is a value to try, adjust and apply again, and the
        // manager holds the commit so a burst of steps is one action.
        .Commit = [services = _services, admin = _admin, target = TargetRef(), act = &spec.Action](
                      int, const int& value) { services.Actions.Run(admin, target, value, *act); },
        .Index = spec.Index,
        .Enabled = Allowed(spec.Action.Permission),
    }
        .ToItem();
}

MenuItem ActionRows::Effect(const EffectDescriptor& effect)
{
    return ToggleRow{
        .Label = Tr(effect.NameKey),
        .On = Tr("effectState.on"),
        .Off = Tr("effectState.off"),
        .Get = [effects = _services.Effects, target = TargetRef(),
                id = effect.Id](int) { return effects && effects->IsActive(target.Slot, id); },
        .Flip =
            [services = _services, admin = _admin, target = TargetRef(), e = &effect](int) {
                if (services.Effects)
                    EffectDispatcher{services.Actions, *services.Effects}.Toggle(admin, target, *e);
            },
        .Enabled = Allowed(effect.Permission),
    }
        .ToItem();
}

std::shared_ptr<Menu> ActionRows::BuildPicker(const EffectDescriptor& effect, bool allowed) const
{
    // Get(PlayerRef), not Get(slot): a picker built for a player who has since left must not
    // reopen against whoever took the slot.
    if (!_target)
        return nullptr;
    auto* targetPlayer = _services.Players.Get(*_target);
    if (!targetPlayer)
        return nullptr;

    MenuBuilder builder(std::format("{}: {}", Tr(effect.NameKey), targetPlayer->Name()));

    auto apply = [services = _services, admin = _admin, target = *_target, e = &effect](int slot, int param) {
        if (services.Effects)
            EffectDispatcher{services.Actions, *services.Effects}.Apply(admin, target, *e, param);
        services.Menus.CloseAll(slot);
    };

    for (const auto& choice : effect.Choices ? effect.Choices() : std::vector<EffectChoice>{})
    {
        builder.Add(ButtonRow{.Label = choice.Label,
                              .Activate = [apply, param = choice.Param](int slot) { apply(slot, param); },
                              .Enabled = allowed});
    }

    if (!effect.ResetLabelKey.empty())
    {
        builder.Add(ButtonRow{.Label = Tr(effect.ResetLabelKey),
                              .Activate =
                                  [services = _services, admin = _admin, target = *_target, e = &effect](int slot) {
                                      if (services.Effects)
                                          EffectDispatcher{services.Actions, *services.Effects}.Clear(admin, target,
                                                                                                      *e);
                                      services.Menus.CloseAll(slot);
                                  },
                              .Enabled = allowed});
    }

    return builder.Build();
}

MenuItem ActionRows::EffectPicker(const EffectDescriptor& effect)
{
    const bool allowed = Allowed(effect.Permission);
    return SubmenuRow{
        .Label = Tr(effect.NameKey),
        .Build = [self = *this, e = &effect, allowed](int) -> std::shared_ptr<Menu> {
            return self.BuildPicker(*e, allowed);
        },
        .Enabled = allowed,
    }
        .ToItem();
}

}  // namespace VoltMod
