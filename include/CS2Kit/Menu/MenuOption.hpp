#pragma once

#include <string>

namespace CS2Kit::Menu
{

/**
 * @brief Polymorphic base for every selectable row in a menu.
 *
 * Subclasses encode the *behavior* of a row (button, toggle, choice picker, slider,
 * text, progress bar, input field, submenu link). The renderer calls @ref GetLabel
 * each frame; the manager calls @ref OnActivate when E is pressed and
 * @ref OnHorizontal when A/D is pressed (the manager falls back to page-jump if
 * `OnHorizontal` returns false).
 *
 * Concrete option types live in `CS2Kit/Menu/Options/`. Most consumers don't need
 * the individual headers - `CS2Kit/Menu/MenuBuilder.hpp` brings them all in.
 */
class MenuOption
{
public:
    virtual ~MenuOption() = default;

    /** Label rendered for this row. Called every frame; safe to read live state. */
    virtual std::string GetLabel(int slot) const = 0;

    /** Non-selectable rows (Text, ProgressBar) are rendered but skipped by W/S navigation. */
    virtual bool IsSelectable() const { return true; }

    /** Disabled rows are rendered greyed out and excluded from cursor stepping. */
    bool IsEnabled() const { return _enabled; }

    /** E key. Called only when the option is selectable and enabled. */
    virtual void OnActivate(int /*slot*/) {}

    /** A/D key. Return true to consume the input; false falls back to page-jump. */
    virtual bool OnHorizontal(int /*slot*/, int /*direction*/) { return false; }

    /** True when A/D edits this row's value (toggle/choice/slider) rather than paging. */
    virtual bool UsesHorizontal() const { return false; }

protected:
    bool _enabled = true;
};

}  // namespace CS2Kit::Menu
