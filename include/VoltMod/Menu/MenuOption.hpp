#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Menu/MenuRow.hpp>
#include <string>

namespace VoltMod
{

/**
 * @brief Polymorphic base for every selectable row in a menu.
 *
 * Subclasses encode the *behavior* of a row (button, toggle, choice picker, slider,
 * text, progress bar, input field, submenu link). The renderer calls @ref Describe
 * each frame; the manager calls @ref OnActivate when E is pressed and
 * @ref OnHorizontal when A/D is pressed (the manager falls back to page-jump if
 * `OnHorizontal` returns false).
 *
 * Concrete option types live in `VoltMod/Menu/Options/`. Most consumers don't need
 * the individual headers - `VoltMod/Menu/MenuBuilder.hpp` brings them all in.
 */
class MenuOption
{
public:
    virtual ~MenuOption() = default;

    /**
     * This row as plain text: what it is called, what it is set to, and what it is.
     *
     * Called every frame, so it is safe to read live state. Plain text on purpose - a driver
     * composes and escapes the two halves the way its own output needs, which is the only way one
     * row can render as both a line of center HTML and a pair of Panorama panels.
     */
    virtual MenuRow Describe(int slot) const = 0;

    /** Non-selectable rows (Text) are rendered but skipped by W/S navigation. */
    virtual bool IsSelectable() const { return true; }

    /** Disabled rows are rendered greyed out and excluded from cursor stepping. */
    bool IsEnabled() const { return _enabled; }

    /** E key. Called only when the option is selectable and enabled. @p menus is the manager
     *  rendering this row, so a row can push a submenu or start a chat prompt without a global. */
    virtual void OnActivate(int /*slot*/, MenuHost& /*menus*/) {}

    /** A/D key. Return true to consume the input; false falls back to page-jump. */
    virtual bool OnHorizontal(int /*slot*/, int /*direction*/) { return false; }

    /** True when A/D edits this row's value (toggle/choice/slider) rather than paging. */
    virtual bool UsesHorizontal() const { return false; }

protected:
    bool _enabled = true;
};

}  // namespace VoltMod
