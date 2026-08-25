#include <VoltMod/Core/ChatColors.hpp>
#include <VoltMod/Core/StringUtils.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <string_view>
#include <utility>

namespace VoltMod::Menu
{

void AppendPlayerRows(MenuBuilder& builder, Players::PlayerManager& players, int viewerSlot,
                      const std::function<void(int viewerSlot, int targetSlot)>& onPick, const std::string& emptyLabel,
                      const std::function<bool(int targetSlot)>& isEnabled)
{
    auto connected = players.GetAllPlayers();
    for (auto* p : connected)
    {
        if (!p)
            continue;
        int targetSlot = p->GetSlot();
        bool enabled = isEnabled ? isEnabled(targetSlot) : true;
        builder.AddButton(
            p->GetName(),
            [viewerSlot, targetSlot, onPick](int /*slot*/) {
                if (onPick)
                    onPick(viewerSlot, targetSlot);
            },
            enabled);
    }

    if (connected.empty() && !emptyLabel.empty())
        builder.AddButton(emptyLabel, [](int) {}, false);
}

std::shared_ptr<MenuView> BuildPlayerPicker(Players::PlayerManager& players, int viewerSlot, const std::string& title,
                                            std::function<void(int viewerSlot, int targetSlot)> onPick,
                                            const std::string& emptyLabel,
                                            std::function<bool(int targetSlot)> isEnabled)
{
    MenuBuilder builder(title);
    AppendPlayerRows(builder, players, viewerSlot, onPick, emptyLabel, isEnabled);
    return builder.Build();
}

std::shared_ptr<MenuView> BuildDurationPicker(int viewerSlot, const std::string& title,
                                              const std::vector<std::pair<std::string, int>>& presets,
                                              std::function<void(int viewerSlot, int seconds)> onPick,
                                              const std::string& customLabel, const std::string& customPrompt,
                                              int maxInputLen)
{
    MenuBuilder builder(title);

    for (const auto& [label, seconds] : presets)
    {
        builder.AddButton(label, [viewerSlot, secs = seconds, onPick](int /*slot*/) {
            if (onPick)
                onPick(viewerSlot, secs);
        });
    }

    if (!customLabel.empty())
    {
        builder.AddInput(
            customLabel, customPrompt, [](int) { return std::string{}; },
            [viewerSlot, onPick](int /*slot*/, std::string_view text) -> bool {
                int seconds = Core::ParseDuration(text);
                if (seconds < 0)
                    return false;  // re-prompt
                if (onPick)
                    onPick(viewerSlot, seconds);
                return true;
            },
            maxInputLen);
    }

    return builder.Build();
}

std::shared_ptr<MenuView> BuildConfirmDialog(MenuManager& menus, ConfirmDialogSpec spec)
{
    MenuBuilder builder(spec.Title);

    for (auto& line : spec.BodyLines)
        builder.AddText(std::move(line));

    builder.AddButton(spec.ConfirmLabel, [onConfirm = std::move(spec.OnConfirm)](int slot) {
        if (onConfirm)
            onConfirm(slot);
    });
    // By pointer, not by reference: the row outlives this call, and `menus` is a local reference.
    builder.AddButton(spec.CancelLabel, [menus = &menus, onCancel = std::move(spec.OnCancel)](int slot) {
        if (onCancel)
            onCancel(slot);
        else
            menus->CloseAllMenus(slot);
    });

    return builder.Build();
}

std::vector<ChoiceOption<std::string>::Choice> BuildPaletteChoices(
    const std::function<std::string(std::string_view canonicalName)>& labelFor)
{
    std::vector<ChoiceOption<std::string>::Choice> choices;
    choices.reserve(Core::ChatColors::Palette.size());

    for (const auto& entry : Core::ChatColors::Palette)
    {
        std::string label = labelFor ? labelFor(entry.Name) : std::string{};
        if (label.empty())
            label = std::string(entry.Name);
        choices.push_back({std::move(label), std::string(entry.Name)});
    }
    return choices;
}

}  // namespace VoltMod::Menu
