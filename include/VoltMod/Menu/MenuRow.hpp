#pragma once

#include <string>

namespace VoltMod
{

/** What a row *is*, for a driver that styles rows rather than spelling them out in their text. */
enum class MenuRowKind
{
    Text,     ///< Not selectable; a caption or a summary line.
    Button,   ///< Runs something.
    Submenu,  ///< Opens another menu.
    Toggle,   ///< Carries an on/off value.
    Choice,   ///< Carries one of several values, stepped left and right.
    Input     ///< Carries a value the player types in chat.
};

/**
 * @brief One row as plain text, split into what it is called and what it is set to.
 *
 * @ref MenuOption::GetLabel composes those into one string with the decoration center HTML wants
 * (`"Speed: &lt; 100% &gt;"`). A Panorama layout has a panel for each half and a stylesheet for
 * the decoration, and no way to read an HTML entity, so it asks for this instead.
 *
 * Plain text throughout: escaping belongs to whichever driver needs it.
 */
struct MenuRow
{
    /** What the row is called: `"Speed"`. */
    std::string Label;
    /** What it is set to: `"100%"`, `"ON"`. Empty for a row that carries no value. */
    std::string Value;
    MenuRowKind Kind = MenuRowKind::Button;
};

}  // namespace VoltMod
