#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Reusable, content-agnostic menu building blocks.
 *
 * These take every human-facing string as a parameter, so they carry no localization of their
 * own - the caller supplies already-translated text - and each takes the single service it needs
 * (`runtime.Players`) rather than the runtime.
 */

/** The player list @ref AppendPlayerRows and @ref BuildPlayerPicker draw. */
struct PlayerPicker
{
    /** Only @ref BuildPlayerPicker uses it; @ref AppendPlayerRows appends into a titled builder. */
    std::string Title;
    /** Runs with the picked player's slot. The viewer is whoever the caller built this for. */
    std::function<void(int targetSlot)> Pick;
    /** Shown as one disabled row when nobody is connected; empty appends nothing. */
    std::string EmptyLabel;
    /** Per-row: false renders that player disabled. Unset enables every row. */
    std::function<bool(int targetSlot)> Enabled;
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
    std::string ConfirmLabel;
    std::string CancelLabel;
    std::function<void(int slot)> Confirm;
    /** Empty closes every menu through the session the dialog is drawn in, which is what cancel
     *  usually means. */
    std::function<void(int slot)> Cancel;
};

/** The menu @ref ConfirmMenu describes. */
std::shared_ptr<Menu> BuildConfirmMenu(ConfirmMenu spec);

/**
 * @ref ChatColors::Palette as choice-row entries (value = canonical color name), so color pickers
 * grow as the palette does. @p labelFor supplies the localized label for each canonical name;
 * returning "" falls back to the name itself.
 */
std::vector<std::pair<std::string, std::string>> BuildPaletteChoices(
    std::function<std::string(std::string_view canonicalName)> labelFor);

}  // namespace VoltMod
