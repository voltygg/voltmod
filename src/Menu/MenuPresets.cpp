#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Messaging/ChatColors.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <string_view>
#include <utility>

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
        builder.Add(ButtonRow{.Label = spec.EmptyLabel, .Enabled = false});
}

std::shared_ptr<Menu> BuildPlayerPicker(PlayerManager& players, PlayerPicker spec)
{
    MenuBuilder builder(spec.Title);
    AppendPlayerRows(builder, players, spec);
    return builder.Build();
}

std::vector<std::pair<std::string, std::string>> BuildPaletteChoices(
    std::function<std::string(std::string_view canonicalName)> labelFor)
{
    std::vector<std::pair<std::string, std::string>> choices;
    choices.reserve(ChatColors::Palette.size());

    for (const auto& entry : ChatColors::Palette)
    {
        std::string label = labelFor ? labelFor(entry.Name) : std::string{};
        if (label.empty())
            label = std::string(entry.Name);
        choices.emplace_back(std::move(label), std::string(entry.Name));
    }
    return choices;
}

}  // namespace VoltMod
