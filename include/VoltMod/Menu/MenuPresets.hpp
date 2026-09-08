#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/** Settings for a player picker. Selections use @ref PlayerRef to handle disconnects safely. */
struct PlayerPicker
{
    std::string Title;
    /** Runs with the selected player. */
    std::function<void(PlayerRef target)> Pick;
    /** Optional submenu factory. Takes precedence over @ref Pick. */
    std::function<std::shared_ptr<Menu>(PlayerRef target)> Open;
    /** Shown as one disabled row when nobody is connected; empty appends nothing. */
    std::string EmptyLabel;
    /** Dynamic enabled state for each player. */
    std::function<bool(PlayerRef target)> Enabled;
};

/** Appends one row per connected player to @p builder. */
void AppendPlayerRows(MenuBuilder& builder, PlayerManager& players, const PlayerPicker& spec);

/** Builds a menu containing only the @ref AppendPlayerRows list. */
std::shared_ptr<Menu> BuildPlayerPicker(PlayerManager& players, PlayerPicker spec);

/** Duration presets with optional free-text input. */
struct DurationMenu
{
    std::string Title;
    /** (label, seconds) pairs, drawn in this order. */
    std::vector<std::pair<std::string, int>> Presets;
    /** Runs with the seconds the picked row - or the typed text - stands for. */
    std::function<void(int slot, int seconds)> Pick;
    /** Empty disables free-text input. */
    std::string CustomLabel;
    /** Shown over the menu while the player types. */
    std::string CustomPrompt;
    /** Longest chat line the free-text row accepts. */
    int MaxInputLength = 32;
};

/** Builds a duration menu. Invalid text prompts again. */
std::shared_ptr<Menu> BuildDurationMenu(DurationMenu spec);

/** Lines listed before a confirm dialog's two buttons. */
class SummaryRows
{
public:
    /** `"{label}: {value}"`, or just `"{label}"` when @p value is empty. */
    SummaryRows& Add(std::string label, std::string value = {})
    {
        _rows.emplace_back(std::move(label), std::move(value));
        return *this;
    }

    /** Adds a row when @p condition is true. */
    SummaryRows& AddIf(bool condition, std::string label, std::string value = {})
    {
        return condition ? Add(std::move(label), std::move(value)) : *this;
    }

    /** The rows, formatted one per line. Consumes them. */
    [[nodiscard]] std::vector<std::string> Take() &&;

private:
    std::vector<std::pair<std::string, std::string>> _rows;
};

/** Settings for a confirmation menu. */
struct ConfirmMenu
{
    std::string Title;
    /** Preformatted text shown above the buttons. */
    std::vector<std::string> Lines;
    /** Empty labels use "Confirm" and "Cancel". */
    std::string ConfirmLabel;
    std::string CancelLabel;
    std::function<void(int slot)> Confirm;
    /** An empty callback closes the menu session. */
    std::function<void(int slot)> Cancel;
};

/** The menu @ref ConfirmMenu describes. */
std::shared_ptr<Menu> BuildConfirmMenu(ConfirmMenu spec);

}  // namespace VoltMod
