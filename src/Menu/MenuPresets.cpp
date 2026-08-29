#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Messaging/ChatColors.hpp>
#include <string>
#include <string_view>
#include <utility>

// The presets that are rows and callbacks and nothing else. The player list needs the roster and
// lives in PlayerRows.cpp; everything here is SDK-free and recompiled by the unit tests.

namespace VoltMod
{

std::shared_ptr<Menu> BuildDurationMenu(DurationMenu spec)
{
    MenuBuilder builder(std::move(spec.Title));

    for (const auto& [label, seconds] : spec.Presets)
    {
        builder.Button(label, [pick = spec.Pick, seconds](int slot) {
            if (pick)
                pick(slot, seconds);
        });
    }

    // An empty label means "no custom row", so a caller can gate it on config without splitting
    // the call.
    if (!spec.CustomLabel.empty())
    {
        builder.Add(InputRow{.Label = std::move(spec.CustomLabel),
                             .Prompt = std::move(spec.CustomPrompt),
                             .Set =
                                 [pick = spec.Pick](int slot, std::string_view text) {
                                     const int seconds = ParseDuration(text);
                                     if (seconds < 0)
                                         return false;  // re-prompt
                                     if (pick)
                                         pick(slot, seconds);
                                     return true;
                                 },
                             .MaxLength = spec.MaxInputLength});
    }

    return builder.Build();
}

std::shared_ptr<Menu> BuildConfirmMenu(ConfirmMenu spec)
{
    MenuBuilder builder(std::move(spec.Title));

    for (const auto& line : spec.Lines)
        builder.Text(line);

    builder.Button(std::move(spec.ConfirmLabel), [confirm = std::move(spec.Confirm)](int slot) {
        if (confirm)
            confirm(slot);
    });

    // Built by hand rather than as a ButtonRow: cancel with no callback of its own closes the
    // menus, and the session to close them through is the one handed to Activate.
    builder.Add(MenuItem{.Describe = [label = std::move(spec.CancelLabel)](int) { return MenuRow{.Label = label}; },
                         .Activate =
                             [cancel = std::move(spec.Cancel)](int slot, MenuSession& session) {
                                 if (cancel)
                                     cancel(slot);
                                 else
                                     session.CloseAll(slot);
                             }});

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
