#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

// The row specs below carry no engine dependency: a spec is text plus callbacks, and the two that
// reach the menu session (InputRow, SubmenuRow) reach it through MenuSession, which is SDK-free
// itself. Every name here is spelled `<Kind>Row` so a spec never collides with the builder method
// of the same kind.

/** A plain action row. @ref Activate runs on E or a click. */
struct ButtonRow
{
    std::string Label;
    std::function<void(int slot)> Activate;
    bool Enabled = true;

    /** This spec as a row. */
    [[nodiscard]] MenuItem ToItem() const;
};

/**
 * A boolean row. Both E and A/D run @ref Flip, and the row reads its state back through
 * @ref Get on every redraw, so the value shown is the one the world holds.
 */
struct ToggleRow
{
    std::string Label;
    std::string On = "ON";
    std::string Off = "OFF";
    std::function<bool(int slot)> Get;
    std::function<void(int slot)> Flip;
    bool Enabled = true;

    [[nodiscard]] MenuItem ToItem() const;
};

/** When a @ref ChoiceRow runs its `Commit`. */
enum class ChoiceApply
{
    /** Stepping applies the value: A/D (or a stepper) picks it, and the row commits once the
     *  presses stop. The default, because a value the player picked and saw is one they asked
     *  for. */
    OnStep,
    /** Only E - or a click on the row - applies it. For a value that must not be tried on the
     *  way past: one that costs something to apply, is destructive, or is announced to everyone
     *  every time it lands. */
    OnSelect
};

/**
 * @brief A row cycling a labeled list of values. A/D walks it (wrapping) and applies what it
 * lands on; @ref Apply says whether that is what happens or whether E has to.
 *
 * The index lives in the row unless @ref GetIndex and @ref SetIndex are supplied, which is how a
 * caller keeps the selection somewhere the rest of the menu can read.
 *
 * With no @ref Commit, E steps forward like D - the shape for a "pick a value" row another part
 * of the menu reads live.
 *
 * @tparam T value carried with each label.
 */
template <class T>
struct ChoiceRow
{
    std::string Label;
    std::vector<std::pair<std::string, T>> Choices;
    /** Runs on E, and - unless @ref Apply says otherwise - a moment after the last step. */
    std::function<void(int slot, const T& value)> Commit;
    /** @{ Optional external index. Supply both or neither; neither keeps the index in the row. */
    std::function<int(int slot)> GetIndex;
    std::function<void(int slot, int index)> SetIndex;
    /** @} */
    int Index = 0;
    bool Enabled = true;
    ChoiceApply Apply = ChoiceApply::OnStep;

    [[nodiscard]] MenuItem ToItem() const;
};

/**
 * A free-text row. E pauses the menu and routes the player's next chat line to @ref Set; a false
 * return re-prompts, and so does text longer than @ref MaxLength. R cancels.
 */
struct InputRow
{
    std::string Label;
    std::string Prompt;
    std::function<std::string(int slot)> Get;
    std::function<bool(int slot, std::string_view text)> Set;
    int MaxLength = 64;
    bool Enabled = true;

    [[nodiscard]] MenuItem ToItem() const;
};

/** A link to another menu. @ref Build runs on E and its result is pushed onto the stack. */
struct SubmenuRow
{
    std::string Label;
    std::function<std::shared_ptr<Menu>(int slot)> Build;
    bool Enabled = true;

    [[nodiscard]] MenuItem ToItem() const;
};

/** A heading or divider. The cursor skips it. */
struct TextRow
{
    std::string Label;

    [[nodiscard]] MenuItem ToItem() const;
};

/**
 * @brief Fluent builder over the row specs above.
 *
 * @code
 * auto menu = MenuBuilder("Admin Panel")
 *     .Subtitle(targetName)
 *     .Text("Punish")
 *     .Button("Kick", [](int slot) { Kick(slot); })
 *     .Add(ToggleRow{.Label = "God mode", .Get = IsGod, .Flip = FlipGod})
 *     .Add(ChoiceRow<int>{.Label = "HP", .Choices = {{"1", 1}, {"100", 100}}, .Commit = SetHealth})
 *     .Build();
 * @endcode
 *
 * Rows that act on an admin/target pair come from @ref ActionRows, which produces @ref MenuItem
 * values this builder appends like any other.
 */
class MenuBuilder
{
public:
    explicit MenuBuilder(std::string title) : _menu(std::make_shared<Menu>()) { _menu->Title = std::move(title); }

    /**
     * A second line under the title: a version, a target's name, what a flow is about to do.
     *
     * Both drivers show it - center HTML on its header line, the Panorama menu in its own panel -
     * so it is plain text with no markup, which is what lets either render it.
     */
    MenuBuilder& Subtitle(std::string subtitle)
    {
        _menu->Subtitle = std::move(subtitle);
        return *this;
    }

    /** Append a row built by hand, or by @ref ActionRows. */
    MenuBuilder& Add(MenuItem item)
    {
        _menu->Items.push_back(std::move(item));
        return *this;
    }

    /** @{ Append one of the row specs. */
    MenuBuilder& Add(const ButtonRow& row) { return Add(row.ToItem()); }
    MenuBuilder& Add(const ToggleRow& row) { return Add(row.ToItem()); }
    MenuBuilder& Add(const InputRow& row) { return Add(row.ToItem()); }
    MenuBuilder& Add(const SubmenuRow& row) { return Add(row.ToItem()); }
    MenuBuilder& Add(const TextRow& row) { return Add(row.ToItem()); }

    template <class T>
    MenuBuilder& Add(const ChoiceRow<T>& row)
    {
        return Add(row.ToItem());
    }
    /** @} */

    /** @ref ButtonRow with nothing but a label and a callback. */
    MenuBuilder& Button(std::string label, std::function<void(int slot)> activate)
    {
        return Add(ButtonRow{.Label = std::move(label), .Activate = std::move(activate)});
    }

    /** @ref SubmenuRow with nothing but a label and a factory. */
    MenuBuilder& Submenu(std::string label, std::function<std::shared_ptr<Menu>(int slot)> build)
    {
        return Add(SubmenuRow{.Label = std::move(label), .Build = std::move(build)});
    }

    /** @ref TextRow: a heading or divider. */
    MenuBuilder& Text(std::string label) { return Add(TextRow{.Label = std::move(label)}); }

    /** Finalize and return the built menu. The builder must not be reused after this. */
    std::shared_ptr<Menu> Build() { return std::move(_menu); }

private:
    std::shared_ptr<Menu> _menu;
};

template <class T>
MenuItem ChoiceRow<T>::ToItem() const
{
    // The row outlives the spec the caller wrote it from, so the spec is copied once into a shared
    // state rather than by value into each callback: all four callbacks below are stored on the
    // MenuItem for as long as the menu is open, and a capture per callback would keep that many
    // copies of Choices alive.
    struct State
    {
        std::vector<std::pair<std::string, T>> Choices;
        std::function<void(int slot, const T& value)> Commit;
        std::function<int(int slot)> GetIndex;
        std::function<void(int slot, int index)> SetIndex;
        std::string Label;
        bool Enabled;
        /** The index the row keeps for itself when the caller supplied no GetIndex/SetIndex. */
        int Own;

        [[nodiscard]] int Read(int slot) const
        {
            if (Choices.empty())
                return 0;
            const int index = GetIndex ? GetIndex(slot) : Own;
            return std::clamp(index, 0, static_cast<int>(Choices.size()) - 1);
        }

        void Write(int slot, int index)
        {
            if (SetIndex)
                SetIndex(slot, index);
            else
                Own = index;
        }

        void Apply(int slot) const
        {
            if (Commit && !Choices.empty())
                Commit(slot, Choices[static_cast<std::size_t>(Read(slot))].second);
        }

        bool Step(int slot, int direction)
        {
            if (Choices.empty())
                return false;
            Write(slot, WrapIndex(Read(slot) + direction, static_cast<int>(Choices.size())));
            return true;
        }
    };

    auto state = std::make_shared<State>(State{.Choices = Choices,
                                               .Commit = Commit,
                                               .GetIndex = GetIndex,
                                               .SetIndex = SetIndex,
                                               .Label = Label,
                                               .Enabled = Enabled,
                                               .Own = Index});

    return MenuItem{
        .Describe =
            [state](int slot) {
                return MenuRow{.Label = state->Label,
                               .Value = state->Choices.empty()
                                            ? std::string{}
                                            : state->Choices[static_cast<std::size_t>(state->Read(slot))].first,
                               .Kind = MenuRowKind::Choice,
                               .Enabled = state->Enabled,
                               .Steppable = true};
            },
        .Activate =
            [state](int slot, MenuSession&) {
                if (!state->Enabled)
                    return;
                // No commit callback: E advances like D, so the row stays interactive for a
                // plain "pick a value" menu with no separate apply step.
                if (state->Commit)
                    state->Apply(slot);
                else
                    (void)state->Step(slot, +1);
            },
        .Step = [state](int slot, int direction) { return state->Enabled && state->Step(slot, direction); },
        // OnSelect leaves this empty, which is what tells the manager not to hold a commit for
        // the row: nothing applies until the row is activated.
        .Commit =
            Apply == ChoiceApply::OnSelect ? std::function<void(int)>{} : std::function<void(int)>([state](int slot) {
                if (state->Enabled)
                    state->Apply(slot);
            }),
    };
}

}  // namespace VoltMod
