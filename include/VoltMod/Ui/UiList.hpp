#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Ui/UiLayout.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** One row's content, as @ref UiList::Set writes it. */
struct UiRow
{
    /** Left-hand text, into `{prefix}{i}_label`'s `text` variable. */
    std::string_view Label;
    /** Right-hand text, into `{prefix}{i}_value`. Empty also drops the row's `HasValue` class. */
    std::string_view Value;
    /**
     * One class applied to the row panel, swapped for whatever this row carried last. Meant for
     * the row's kind (`"Kind--toggle"`), and empty leaves the row with none. Pass a constant:
     * every distinct class name is interned into the entity's table for the rest of the map.
     */
    std::string_view Modifier;
    /** False adds `Disabled`, which is styling only - a disabled row still reports its press. */
    bool Enabled = true;
    /** True adds `HasSteppers`, showing this row's `_dec`/`_inc` buttons. */
    bool Steppers = false;
};

/**
 * @brief A fixed run of `{prefix}{i}` rows in a layout, written by index and clicked back.
 *
 * The reusable half of a list-shaped panel: a layout that declares the ids below can be driven by
 * this whoever authored it, so a plugin reskinning the framework menu - or building its own
 * roster, vote or shop panel - writes rows rather than dialog variables.
 *
 * For a row @p i, with @p prefix `vm_row`:
 *
 * | Id | What it is |
 * | --- | --- |
 * | `vm_row3` | the row `Button`; carries `Hidden`, `Disabled`, `HasValue`, `HasSteppers` |
 * | `vm_row3_label`, `vm_row3_value` | `Label`s reading `text="{s:text}"` |
 * | `vm_row3_dec`, `vm_row3_inc` | the row's stepper `Button`s |
 *
 * Rows past what @ref Set was given are hidden by @ref HideFrom, so a short page leaves no stale
 * text on screen. Ids are built once at construction, and writes go through @ref UiLayout, so a
 * frame that changes one row costs one write.
 */
class UiList
{
public:
    /**
     * @param capacity how many rows the layout declares; writing past it is ignored. It has to
     *                 match the markup, which the server cannot see - a layout with eight rows
     *                 and a list of ten silently loses two.
     * @p layout must outlive this object.
     */
    UiList(UiLayout& layout, std::string_view prefix, int capacity);

    UiList(const UiList&) = delete;
    UiList& operator=(const UiList&) = delete;

    /** How many rows the layout declares. */
    [[nodiscard]] int Capacity() const noexcept { return static_cast<int>(_ids.size()); }

    /** Write row @p index for @p slot and show it. Out-of-range indices are ignored. */
    void Set(int slot, int index, const UiRow& row);

    /** Hide rows @p index and after for @p slot. Call it once after the last @ref Set. */
    void HideFrom(int slot, int index);

    /** A row `Button` was pressed: (slot, index). */
    Event<int, int> Pressed;

    /** A row's stepper was pressed: (slot, index, direction), -1 for `_dec` and +1 for `_inc`. */
    Event<int, int, int> Stepped;

private:
    /** Every id one row needs, built once so a frame allocates nothing. */
    struct RowIds
    {
        std::string Row;
        std::string Label;
        std::string Value;
        std::string Dec;
        std::string Inc;
    };

    /** Subscribe to every row's buttons. Deferred to the first @ref Set: subscribing is what
     *  installs the click hook, and a list nobody has drawn should not arm one. */
    void Bind();

    UiLayout& _layout;
    std::vector<RowIds> _ids;
    /** The @ref UiRow::Modifier each row currently carries, so the next one can replace it. */
    PerSlot<std::vector<std::string>> _modifiers;
    /** Declared after everything their handlers touch. */
    std::vector<Subscription> _clicks;
};

}  // namespace VoltMod
