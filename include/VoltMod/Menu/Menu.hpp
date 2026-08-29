#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <vector>

namespace VoltMod
{

/** Maximum items shown per page before the menu paginates. */
inline constexpr int ItemsPerPage = 5;

/** Pages @p items need at @p perPage each, never less than one so an empty menu still draws. */
constexpr int PageCount(int items, int perPage)
{
    return items <= 0 ? 1 : (items + perPage - 1) / perPage;
}

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
 * @brief One row as a driver draws it: what it is called, what it is set to, and how it behaves.
 *
 * Produced by @ref MenuItem::Describe on every redraw, so every field is live - a row that greys
 * out while the menu is open greys out on the next frame.
 *
 * Plain text throughout: escaping belongs to whichever driver needs it. Center HTML composes the
 * two halves into one line (`"Speed: &lt; 100% &gt;"`); a Panorama layout has a panel for each
 * half and a stylesheet for the decoration.
 */
struct MenuRow
{
    /** What the row is called: `"Speed"`. */
    std::string Label;
    /** What it is set to: `"100%"`, `"ON"`. Empty for a row that carries no value. */
    std::string Value;
    MenuRowKind Kind = MenuRowKind::Button;
    /** Disabled rows are drawn greyed out, skipped by the cursor, and refuse activation. */
    bool Enabled = true;
    /** False for a row the cursor may not land on (Text). */
    bool Selectable = true;
    /** True when A/D - or the row's steppers - change the value rather than paging. */
    bool Steppable = false;
    /** An on/off row's state, so a driver can draw a switch instead of reading @ref Value. */
    std::optional<bool> State;
};

/**
 * @brief One row's behaviour, as values.
 *
 * A row is data, not a subclass: @ref MenuBuilder's row specs produce these, and a consumer with
 * a shape the specs do not cover fills one in itself.
 *
 * @ref Describe is required and runs on every redraw. The rest may be empty: a row with no
 * @ref Activate does nothing when pressed (a Text row), and one with no @ref Step leaves A/D to
 * the driver, which pages instead.
 */
struct MenuItem
{
    /** This row, right now. Called every redraw, so it is safe to read live state. */
    std::function<MenuRow(int slot)> Describe;

    /** E, or a click. @p menus is the host showing the row, so a row can push a submenu or start
     *  a chat prompt without reaching for a global. */
    std::function<void(int slot, MenuHost& menus)> Activate;

    /** A/D, or a stepper press: @p direction is -1 or +1. Return true to consume the input;
     *  false (or an empty callback) lets the driver page instead. */
    std::function<bool(int slot, int direction)> Step;

    /** Apply whatever @ref Step left the row showing. Empty when stepping already applied it. */
    std::function<void(int slot)> Commit;
};

/** A menu, as either @ref MenuHost shows it. Build with MenuBuilder. */
struct Menu
{
    std::string Title;
    /** Shown next to the title, smaller and dimmer: a version, a breadcrumb, a target's name.
     *  Plain text - it is markup in neither driver. */
    std::string Subtitle;
    std::vector<MenuItem> Items;
};

/**
 * Per-player menu runtime state held by MenuHost. The stack supports
 * submenus: opening pushes, R or programmatic close pops back to the parent.
 */
struct PlayerMenuState
{
    /** The stack of menus currently open for the player. */
    std::stack<std::shared_ptr<Menu>> MenuStack;
    int SelectedIndex = 0;
    /** Which page of a long menu is showing. Center HTML derives its page from SelectedIndex
     *  instead; a click driver has no cursor to derive one from, so it keeps this. */
    int Page = 0;
    int64_t LastInputTime = 0;
    uint64_t PrevButtons = 0;

    /** True while MenuHost is holding the player's movement frozen for this menu session. */
    bool MovementFrozen = false;
    /** MoveType captured before freezing, restored when the menu closes. */
    MoveType PrevMoveType = MoveType::Walk;

    /** True if the player has any menu currently open. */
    bool HasMenu() const { return !MenuStack.empty(); }
    /** Top of the stack, or nullptr if no menu is open. */
    Menu* GetCurrentMenu() { return MenuStack.empty() ? nullptr : MenuStack.top().get(); }

    /** Clears the entire menu stack and resets selection/input state. */
    void Reset() { *this = {}; }
};

}  // namespace VoltMod
