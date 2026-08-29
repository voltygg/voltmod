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
 * @ref ChatColors::Palette as choice-row entries (value = canonical color name), so color pickers
 * grow as the palette does. @p labelFor supplies the localized label for each canonical name;
 * returning "" falls back to the name itself.
 */
std::vector<std::pair<std::string, std::string>> BuildPaletteChoices(
    std::function<std::string(std::string_view canonicalName)> labelFor);

}  // namespace VoltMod
