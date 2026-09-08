#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Players/PlayerManager.hpp>

namespace VoltMod
{

void AppendPlayerRows(MenuBuilder& builder, PlayerManager& players, const PlayerPicker& spec)
{
    auto connected = players.All();
    for (auto* player : connected)
    {
        // Resolve the original player, not a later occupant of the slot.
        const PlayerRef target = player->Ref();

        EnabledCondition enabled = spec.Enabled
                                       ? EnabledCondition([check = spec.Enabled, target](int) { return check(target); })
                                       : EnabledCondition(true);

        // Drivers escape this plain-text name for their output format.
        if (spec.Open)
        {
            builder.Add(SubmenuRow{.Label = player->Name(),
                                   .Build = [open = spec.Open, target](int) { return open(target); },
                                   .Enabled = std::move(enabled)});
            continue;
        }

        builder.Add(ButtonRow{.Label = player->Name(),
                              .Activate =
                                  [pick = spec.Pick, target](int) {
                                      if (pick)
                                          pick(target);
                                  },
                              .Enabled = std::move(enabled)});
    }

    if (connected.empty() && !spec.EmptyLabel.empty())
        builder.Text(spec.EmptyLabel);
}

std::shared_ptr<Menu> BuildPlayerPicker(PlayerManager& players, PlayerPicker spec)
{
    MenuBuilder builder(spec.Title);
    AppendPlayerRows(builder, players, spec);
    return builder.Build();
}

}  // namespace VoltMod
