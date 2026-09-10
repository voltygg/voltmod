#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Returns the number of pages needed for @p items, with at least one page. */
constexpr int PageCount(int items, int perPage)
{
    return items <= 0 ? 1 : (items + perPage - 1) / perPage;
}

/** Wraps @p value into `[0, count)`, including when @p value is negative. */
constexpr int WrapIndex(int value, int count)
{
    return count <= 0 ? 0 : ((value % count) + count) % count;
}

/** Row type used by renderers. */
enum class MenuRowKind
{
    Text,     ///< Not selectable; a caption or a summary line.
    Button,   ///< Runs something.
    Submenu,  ///< Opens another menu.
    Toggle,   ///< Carries an on/off value.
    Choice,   ///< Carries one of several values, stepped left and right.
    Input     ///< Carries a value the player types in chat.
};

/** Current display state of a menu row. Text is unescaped. */
struct MenuRow
{
    std::string Label;
    /** Empty when the row has no value. */
    std::string Value;
    MenuRowKind Kind = MenuRowKind::Button;
    /** Disabled rows are drawn greyed out, skipped by the cursor, and refuse activation. */
    bool Enabled = true;
    /** Whether the cursor may land on the row. */
    bool Selectable = true;
    /** Whether A/D or a row stepper changes the value instead of paging. */
    bool Steppable = false;
    /** Toggle state, used to render a switch or supply the default value text. */
    std::optional<bool> State;

    /** Set by the menu service, not by @ref MenuItem::Describe. */
    bool Pending = false;
    bool Changed = false;
};

struct Menu;

/** The surface a menu is drawn on, and what a row callback may ask of it. */
class MenuSurface
{
public:
    virtual ~MenuSurface() = default;

    MenuSurface(const MenuSurface&) = delete;
    MenuSurface& operator=(const MenuSurface&) = delete;

    /** Pushes @p menu onto @p slot's stack and shows it. */
    virtual void Open(int slot, std::shared_ptr<Menu> menu) = 0;

    /** Pops the top menu, returning to its parent when one exists. */
    virtual void Close(int slot) = 0;

    /** Clears the stack and removes the menu from the player's screen. */
    virtual void CloseAll(int slot) = 0;

    /** Translates and sends @p replyKey, then closes every menu. */
    virtual void CloseAll(int slot, std::string_view replyKey) = 0;

    /** Routes the next chat line to @p callback. Closing the menu cancels it. */
    virtual void Prompt(int slot, std::string prompt,
                        std::function<bool(int slot, std::string_view text)> callback) = 0;

    /** Translates @p key for @p slot, using @p fallback when missing. */
    [[nodiscard]] virtual std::string Translate(int slot, std::string_view key, std::string_view fallback) const = 0;

protected:
    MenuSurface() = default;
};

/** Row callbacks. Only @ref Describe is required. */
struct MenuItem
{
    /** Describes the row's current state. Called on every redraw. */
    std::function<MenuRow(int slot)> Describe;

    /** Runs on E or a click. */
    std::function<void(int slot, MenuSurface& surface)> Activate;

    /** Runs on A/D or a stepper. Return true to consume the input. */
    std::function<bool(int slot, int direction)> Step;

    /** Applies a stepped value after input settles. */
    std::function<void(int slot)> Commit;
};

/** True when @p item is one a player may act on right now: it describes itself as both enabled
 *  and selectable. A cursor lands only on these, and only these activate. */
[[nodiscard]] inline bool IsRowActionable(const MenuItem& item, int slot)
{
    if (!item.Describe)
        return false;

    const MenuRow row = item.Describe(slot);
    return row.Enabled && row.Selectable;
}

/** A menu model. Build a separate instance for each player. */
struct Menu
{
    std::string Title;
    /** Optional plain-text detail shown with the title. */
    std::string Subtitle;
    std::vector<MenuItem> Items;
};

}  // namespace VoltMod
