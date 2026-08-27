#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Messaging/ChatColors.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <string_view>
#include <utility>

namespace VoltMod
{

void AppendPlayerRows(MenuBuilder& builder, PlayerManager& players, int viewerSlot,
                      const std::function<void(int viewerSlot, int targetSlot)>& onPick, const std::string& emptyLabel,
                      const std::function<bool(int targetSlot)>& isEnabled)
{
    auto connected = players.All();
    for (auto* p : connected)
    {
        int targetSlot = p->Slot();
        bool enabled = isEnabled ? isEnabled(targetSlot) : true;
        builder.Button(
            Strings::EscapeHtml(p->Name()),
            [viewerSlot, targetSlot, onPick](int /*slot*/) {
                if (onPick)
                    onPick(viewerSlot, targetSlot);
            },
            enabled);
    }

    if (connected.empty() && !emptyLabel.empty())
        builder.Button(emptyLabel, [](int) {}, false);
}

std::shared_ptr<MenuView> BuildPlayerPicker(PlayerManager& players, int viewerSlot, const std::string& title,
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
        builder.Button(label, [viewerSlot, secs = seconds, onPick](int /*slot*/) {
            if (onPick)
                onPick(viewerSlot, secs);
        });
    }

    if (!customLabel.empty())
    {
        builder.Input(
            customLabel, customPrompt, [](int) { return std::string{}; },
            [viewerSlot, onPick](int /*slot*/, std::string_view text) -> bool {
                int seconds = ParseDuration(text);
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
        builder.Text(std::move(line));

    builder.Button(spec.ConfirmLabel, [onConfirm = std::move(spec.OnConfirm)](int slot) {
        if (onConfirm)
            onConfirm(slot);
    });
    // By pointer, not by reference: the row outlives this call, and `menus` is a local reference.
    builder.Button(spec.CancelLabel, [menus = &menus, onCancel = std::move(spec.OnCancel)](int slot) {
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
    choices.reserve(ChatColors::Palette.size());

    for (const auto& entry : ChatColors::Palette)
    {
        std::string label = labelFor ? labelFor(entry.Name) : std::string{};
        if (label.empty())
            label = std::string(entry.Name);
        choices.push_back({std::move(label), std::string(entry.Name)});
    }
    return choices;
}

}  // namespace VoltMod
