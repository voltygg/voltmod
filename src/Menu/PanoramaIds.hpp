#pragma once

#include <string_view>

namespace VoltMod
{

/** What a Button in the framework's menu layout stands for. */
enum class MenuButton
{
    None,    ///< Not one of the menu's ids.
    Row,     ///< `vm_row{i}_btn`: activate the row.
    RowDec,  ///< `vm_row{i}_dec`: step the row's value down.
    RowInc,  ///< `vm_row{i}_inc`: step the row's value up.
    Back,    ///< `vm_back`
    Close,   ///< `vm_close`
    Prev,    ///< `vm_prev`
    Next,    ///< `vm_next`
    Cancel   ///< `vm_cancel`: drop the chat prompt.
};

/** One press, as the layout named it. */
struct MenuPress
{
    MenuButton Button = MenuButton::None;
    /** Which `vm_row{i}` of the current page it came from; -1 for anything but a row. */
    int Row = -1;
};

/**
 * The menu layout's Button ids, parsed.
 *
 * One handler over @ref UiPanel::Clicked reads every press, so the layout's id contract lives
 * here rather than in a subscription per id. The text is client-controlled, so anything that is
 * not exactly one of the ids - a wrong prefix, a row index with a leading zero or a sign, a
 * suffix the layout does not declare - is @ref MenuButton::None rather than a guess. SDK-free so
 * the rules are unit-tested.
 */
[[nodiscard]] MenuPress ParseMenuButton(std::string_view id);

}  // namespace VoltMod
