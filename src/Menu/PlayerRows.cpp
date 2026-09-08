#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Players/PlayerManager.hpp>

// The one preset that reads the roster, and the reason it is not in MenuPresets.cpp: a name comes
// off a controller, which puts this translation unit on the far side of the SDK from the dialogs
// the unit tests recompile.

namespace VoltMod
{

void AppendPlayerRows(MenuBuilder& builder, PlayerManager& players, const PlayerPicker& spec)
{
    auto connected = players.All();
    for (auto* player : connected)
    {
        // Who the row was drawn for, not where they were sitting: a slot that changes hands
        // resolves to nobody rather than to whoever took it.
        const PlayerRef target = player->Ref();

        Condition enabled =
            spec.Enabled ? Condition([check = spec.Enabled, target](int) { return check(target); }) : Condition(true);

        // The name goes in raw: a row carries plain text and whichever driver renders it escapes
        // for its own output.
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
