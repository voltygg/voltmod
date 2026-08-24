#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/Options/ChoiceOption.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod::Menu
{

/**
 * @brief Reusable, content-agnostic menu builders.
 *
 * These presets take every human-facing string as a parameter so they carry no
 * localization of their own - the caller supplies already-translated text.
 */

// Forward declaration; full definition in MenuBuilder.hpp.
class MenuBuilder;

/**
 * Append one row per connected player (from the kit PlayerManager) to @p builder - the body of
 * @ref BuildPlayerPicker, exposed so callers can put their own rows above the player list.
 * Selecting a player invokes @p onPick(viewerSlot, targetSlot). @p isEnabled, when supplied,
 * decides per-row whether a target is selectable. If no players are connected, a single
 * disabled @p emptyLabel row is appended instead.
 */
void AppendPlayerRows(MenuBuilder& builder, int viewerSlot,
                      const std::function<void(int viewerSlot, int targetSlot)>& onPick,
                      const std::string& emptyLabel = "", const std::function<bool(int targetSlot)>& isEnabled = {});

/** Build a paginated picker menu containing only the @ref AppendPlayerRows player list. */
std::shared_ptr<MenuView> BuildPlayerPicker(int viewerSlot, const std::string& title,
                                            std::function<void(int viewerSlot, int targetSlot)> onPick,
                                            const std::string& emptyLabel = "",
                                            std::function<bool(int targetSlot)> isEnabled = {});

/**
 * Build a duration picker. Each @p presets entry is a (label, seconds) pair rendered as a row;
 * selecting it invokes @p onPick(viewerSlot, seconds). When @p customLabel is non-empty an
 * extra chat-input row is appended: it prompts with @p customPrompt and parses the typed text
 * via @ref VoltMod::Core::ParseDuration (rejecting/re-prompting on a negative result).
 */
std::shared_ptr<MenuView> BuildDurationPicker(int viewerSlot, const std::string& title,
                                              const std::vector<std::pair<std::string, int>>& presets,
                                              std::function<void(int viewerSlot, int seconds)> onPick,
                                              const std::string& customLabel = "", const std::string& customPrompt = "",
                                              int maxInputLen = 32);

/** Confirmation dialog content; every human-facing string is caller-supplied. */
struct ConfirmDialogSpec
{
    std::string Title;
    std::vector<std::string> BodyLines; /**< Read-only summary rows shown above the buttons. */
    std::string ConfirmLabel;
    std::string CancelLabel;
    std::function<void(int slot)> OnConfirm;
    std::function<void(int slot)> OnCancel; /**< Empty = close all of the player's menus. */
};

/** Build a confirmation dialog: body text rows followed by confirm/cancel buttons. */
std::shared_ptr<MenuView> BuildConfirmDialog(ConfirmDialogSpec spec);

/**
 * Render @ref Core::ChatColors::Palette as ChoiceOption choices (value = canonical color name),
 * so color pickers grow automatically as the palette does. @p labelFor supplies the localized
 * label for each canonical name; return "" to fall back to the name itself.
 */
std::vector<ChoiceOption<std::string>::Choice> BuildPaletteChoices(
    const std::function<std::string(std::string_view canonicalName)>& labelFor);

}  // namespace VoltMod::Menu
