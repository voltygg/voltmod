#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

// Declared here and defined at the bottom of this header: a row hands its Activate a session, and
// a session opens a menu made of rows, so the three have to be written in one file whichever way
// round they go.
struct Menu;

/**
 * @brief What a row may do to the session it is drawn in.
 *
 * Implemented by @ref MenuManager, and named here rather than there so a row - and the
 * @ref MenuBuilder and @ref Flow that produce rows - stays plain values with no engine behind it.
 * That is what lets both be unit-tested against a session that only records what it was asked for.
 *
 * A session runs from the first menu opened for a player until their stack empties.
 */
class MenuSession
{
public:
    virtual ~MenuSession() = default;

    MenuSession(const MenuSession&) = delete;
    MenuSession& operator=(const MenuSession&) = delete;

    /** Push @p menu onto @p slot's stack and start showing it. */
    virtual void Open(int slot, std::shared_ptr<Menu> menu) = 0;

    /** Pop the top menu, falling back to the parent if one exists. */
    virtual void Close(int slot) = 0;

    /** Clear the entire stack and take the menu off the player's screen. */
    virtual void CloseAll(int slot) = 0;

    /** Abort with an explanation: @p replyKey is translated in the player's language and sent
     *  through `Policy::Reply`, then every menu closes. The abort path for a flow whose
     *  preconditions stopped holding. */
    virtual void CloseAll(int slot, std::string_view replyKey) = 0;

    /** Route the player's next chat line to @p callback, showing @p prompt over the open menu.
     *  Rows use this instead of reaching for the runtime's ChatInput themselves. */
    virtual void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback) = 0;

protected:
    MenuSession() = default;
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

    /** E, or a click. @p session is the session showing the row, so a row can push a submenu or
     *  start a chat prompt without reaching for a global. */
    std::function<void(int slot, MenuSession& session)> Activate;

    /** A/D, or a stepper press: @p direction is -1 or +1. Return true to consume the input;
     *  false (or an empty callback) lets the driver page instead. */
    std::function<bool(int slot, int direction)> Step;

    /** Apply whatever @ref Step left the row showing. Empty when stepping already applied it. */
    std::function<void(int slot)> Commit;
};

/** A menu, however @ref MenuManager is drawing menus right now. Build with MenuBuilder. */
struct Menu
{
    std::string Title;
    /** Shown next to the title, smaller and dimmer: a version, a breadcrumb, a target's name.
     *  Plain text - it is markup in neither driver. */
    std::string Subtitle;
    std::vector<MenuItem> Items;
};

}  // namespace VoltMod
