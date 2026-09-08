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

/**
 * The player list @ref AppendPlayerRows and @ref BuildPlayerPicker draw.
 *
 * A row reports a @ref PlayerRef, not a slot: a menu can sit open across a disconnect, and a bare
 * slot would hand the press to whoever took it. `PlayerManager::Get` answers with nobody instead.
 */
struct PlayerPicker
{
    /** Only @ref BuildPlayerPicker uses it; @ref AppendPlayerRows appends into a titled builder. */
    std::string Title;
    /** Runs with the picked player. The viewer is whoever the caller built this for. */
    std::function<void(PlayerRef target)> Pick;
    /** The menu a pick opens, pushed onto the session. Takes precedence over @ref Pick; a null
     *  return pushes nothing, for a target that has gone. */
    std::function<std::shared_ptr<Menu>(PlayerRef target)> Open;
    /** Shown as one disabled row when nobody is connected; empty appends nothing. */
    std::string EmptyLabel;
    /** Per-row, re-asked on every redraw: false renders that player disabled. */
    std::function<bool(PlayerRef target)> Enabled;
};

/** Append one row per connected player to @p builder, so a caller can put its own rows above the
 *  list. */
void AppendPlayerRows(MenuBuilder& builder, PlayerManager& players, const PlayerPicker& spec);

/** A menu holding only the @ref AppendPlayerRows list. */
std::shared_ptr<Menu> BuildPlayerPicker(PlayerManager& players, PlayerPicker spec);

/**
 * @brief A duration picker: one row per preset, plus an optional free-text row.
 *
 * What @ref Flow::AddDurationStep opens, and usable on its own for a menu that asks for a length
 * of time without a whole flow behind it.
 */
struct DurationMenu
{
    std::string Title;
    /** (label, seconds) pairs, drawn in this order. */
    std::vector<std::pair<std::string, int>> Presets;
    /** Runs with the seconds the picked row - or the typed text - stands for. */
    std::function<void(int slot, int seconds)> Pick;
    /** Empty = no free-text row, so a caller can gate it on config without splitting the call. */
    std::string CustomLabel;
    /** Shown over the menu while the player types. */
    std::string CustomPrompt;
    /** Longest chat line the free-text row accepts. */
    int MaxInputLength = 32;
};

/** The menu @ref DurationMenu describes. Typed text is read with @ref ParseDuration, and anything
 *  it refuses re-prompts rather than picking a value nobody asked for. */
std::shared_ptr<Menu> BuildDurationMenu(DurationMenu spec);

/** The lines a confirm dialog lists before its two buttons. */
class SummaryRows
{
public:
    /** `"{label}: {value}"`, or just `"{label}"` when @p value is empty. */
    SummaryRows& Add(std::string label, std::string value = {})
    {
        _rows.emplace_back(std::move(label), std::move(value));
        return *this;
    }

    /** @ref Add only when @p condition holds - a duration line for a punishment that has one. */
    SummaryRows& AddIf(bool condition, std::string label, std::string value = {})
    {
        return condition ? Add(std::move(label), std::move(value)) : *this;
    }

    /** The rows, formatted one per line. Consumes them. */
    [[nodiscard]] std::vector<std::string> Take() &&;

private:
    std::vector<std::pair<std::string, std::string>> _rows;
};

/**
 * @brief A confirm dialog: what is about to happen, then a confirm row and a cancel row.
 *
 * What @ref Flow::Confirm ends with, and usable on its own for any "are you sure" a plugin puts
 * in front of an action.
 */
struct ConfirmMenu
{
    std::string Title;
    /** Drawn above the two rows, one inert line each. Already formatted: the dialog adds no
     *  punctuation of its own. */
    std::vector<std::string> Lines;
    /** Empty falls back to "Confirm" / "Cancel"; @ref Flow translates them before it gets here. */
    std::string ConfirmLabel;
    std::string CancelLabel;
    std::function<void(int slot)> Confirm;
    /** Empty closes every menu through the session the dialog is drawn in, which is what cancel
     *  usually means. */
    std::function<void(int slot)> Cancel;
};

/** The menu @ref ConfirmMenu describes. */
std::shared_ptr<Menu> BuildConfirmMenu(ConfirmMenu spec);

}  // namespace VoltMod
