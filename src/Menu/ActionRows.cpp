#include <VoltMod/Menu/ActionRows.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

ActionRows::ActionRows(const Services& services, PlayerRef admin, std::optional<PlayerRef> target)
    : _services(std::make_shared<const Services>(services)), _admin(admin), _target(std::move(target))
{}

EnabledCondition ActionRows::Allows(std::string_view permission) const
{
    return EnabledCondition(
        [services = _services, admin = _admin, target = _target, permission = std::string(permission)](int) {
            return services->Policy.Authorize(admin, target, permission).has_value();
        });
}

EffectDispatcher ActionRows::Effects() const
{
    return EffectDispatcher{_services->Actions, *_services->Effects};
}

std::string ActionRows::Tr(std::string_view key, Tokens tokens) const
{
    return _services->Translations.Get(key, _admin.Slot, tokens);
}

MenuItem ActionRows::Action(std::string_view labelKey, const VoltMod::Action& action)
{
    // Actions are static plugin data; rows may outlive the ActionRows that created them.
    return ButtonRow{
        .Label = Tr(labelKey),
        .Activate = [services = _services, admin = _admin, target = TargetRef(),
                     act = &action](int) { services->Actions.Run(admin, target, *act); },
        .Enabled = Allows(action.Permission),
    }
        .ToItem();
}

MenuItem ActionRows::StateToggle(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                                 const VoltMod::Action& action)
{
    return ToggleRow{
        .Label = Tr(labelKey),
        .Get =
            [services = _services, target = TargetRef(), isActive = std::move(isActive)](int) {
                Pawn pawn = services->Entities.PawnOf(target.Slot);
                return pawn && isActive(pawn);
            },
        .Flip = [services = _services, admin = _admin, target = TargetRef(),
                 act = &action](int) { services->Actions.Run(admin, target, *act); },
        .Enabled = Allows(action.Permission),
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
        // Keep the menu open so a preset can be adjusted and applied again.
        .Commit = [services = _services, admin = _admin, target = TargetRef(), act = &spec.Action](
                      int, const int& value) { services->Actions.Run(admin, target, value, *act); },
        .Index = spec.Index,
        .Enabled = Allows(spec.Action.Permission),
    }
        .ToItem();
}

EnabledCondition ActionRows::EffectAllows(const EffectDescriptor& effect) const
{
    return _services->Effects ? Allows(effect.Permission) : EnabledCondition(false);
}

MenuItem ActionRows::Effect(const EffectDescriptor& effect)
{
    return ToggleRow{
        .Label = Tr(effect.NameKey),
        .Get = [effects = _services->Effects, target = TargetRef(),
                id = effect.Id](int) { return effects && effects->IsActive(target.Slot, id); },
        .Flip = [self = *this, target = TargetRef(),
                 e = &effect](int) { self.Effects().Toggle(self._admin, target, *e); },
        .Enabled = EffectAllows(effect),
    }
        .ToItem();
}

std::shared_ptr<Menu> ActionRows::BuildPicker(const EffectDescriptor& effect, EnabledCondition allowed) const
{
    // Resolve the captured player, not a later occupant of the slot.
    if (!_target)
        return nullptr;
    auto* targetPlayer = _services->Players.Get(*_target);
    if (!targetPlayer)
        return nullptr;

    MenuBuilder builder(std::format("{}: {}", Tr(effect.NameKey), targetPlayer->Name()));

    auto apply = [self = *this, target = *_target, e = &effect](int slot, int param) {
        self.Effects().Apply(self._admin, target, *e, param);
        self._services->Menus.CloseAll(slot);
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
                                  [self = *this, target = *_target, e = &effect](int slot) {
                                      self.Effects().Clear(self._admin, target, *e);
                                      self._services->Menus.CloseAll(slot);
                                  },
                              .Enabled = allowed});
    }

    return builder.Build();
}

MenuItem ActionRows::EffectPicker(const EffectDescriptor& effect)
{
    EnabledCondition allowed = EffectAllows(effect);
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
