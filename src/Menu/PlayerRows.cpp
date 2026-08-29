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
        int targetSlot = player->Slot();
        // The name goes in raw: a row carries plain text and whichever driver renders it escapes
        // for its own output.
        builder.Add(ButtonRow{.Label = player->Name(),
                              .Activate =
                                  [targetSlot, pick = spec.Pick](int) {
                                      if (pick)
                                          pick(targetSlot);
                                  },
                              .Enabled = spec.Enabled ? spec.Enabled(targetSlot) : true});
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
