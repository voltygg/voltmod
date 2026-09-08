#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Whether a row is usable: a fixed `bool`, or a check re-asked on every redraw.
 *
 * It guards the row's behaviour as well as its label, so a permission revoked while the menu is
 * open greys the row out *and* refuses the press - written once, not once per handler.
 */
class Condition
{
public:
    Condition() = default;
    Condition(bool value) : _fixed(value) {}
    Condition(std::function<bool(int slot)> predicate) : _predicate(std::move(predicate)) {}

    [[nodiscard]] bool operator()(int slot) const { return _predicate ? _predicate(slot) : _fixed; }

private:
    std::function<bool(int slot)> _predicate;
    bool _fixed = true;
};

/** A row spec: anything that turns itself into a @ref MenuItem, so a plugin can write its own row
 *  without a framework change. `ToItem` consumes the spec. */
template <class T>
concept MenuRowSpec = requires(T row) {
    { std::move(row).ToItem() } -> std::same_as<MenuItem>;
};

/** A plain action row. @ref Activate runs on E or a click. */
struct ButtonRow
{
    std::string Label;
    std::function<void(int slot)> Activate;
    Condition Enabled;

    /** This spec as a row; consumes the spec. */
    [[nodiscard]] MenuItem ToItem() &&;
};

/**
 * A boolean row, drawn as a switch. Both E and A/D run @ref Flip, and the row reads its state back
 * through @ref Get on every redraw. The on/off *words* are the framework's (`menu.on` /
 * `menu.off`), so every switch reads the same; a Panorama layout draws the switch and shows none.
 */
struct ToggleRow
{
    std::string Label;
    std::function<bool(int slot)> Get;
    std::function<void(int slot)> Flip;
    Condition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
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

/** Where a @ref ChoiceRow's selection lives when the caller keeps it outside the row. One value
 *  rather than two callbacks, so a getter cannot be supplied without its setter. */
struct ChoiceIndex
{
    std::function<int(int slot)> Get;
    std::function<void(int slot, int index)> Set;
};

/**
 * @brief A row cycling a labeled list of values. A/D walks it (wrapping) and applies what it
 * lands on; @ref Apply says whether that is what happens or whether E has to.
 *
 * The index lives in the row unless @ref Bind is supplied, which is how a caller keeps the
 * selection somewhere the rest of the menu can read.
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
    /** Optional external home for the selection; unset keeps it in the row. */
    std::optional<ChoiceIndex> Bind;
    /** Where an unbound row starts. */
    int Index = 0;
    Condition Enabled;
    ChoiceApply Apply = ChoiceApply::OnStep;

    [[nodiscard]] MenuItem ToItem() &&;
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
    Condition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** A link to another menu. @ref Build runs on E and its result is pushed onto the stack. */
struct SubmenuRow
{
    std::string Label;
    std::function<std::shared_ptr<Menu>(int slot)> Build;
    Condition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** A heading or divider. The cursor skips it. */
struct TextRow
{
    std::string Label;

    [[nodiscard]] MenuItem ToItem() &&;
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
 *     .EmptyText("Nothing to do here")
 *     .Build();
 * @endcode
 *
 * Rows that act on an admin/target pair come from @ref ActionRows, which produces @ref MenuItem
 * values this builder appends like any other.
 */
class MenuBuilder
{
public:
    explicit MenuBuilder(std::string title) { _menu.Title = std::move(title); }

    /**
     * A second line under the title: a version, a target's name, what a flow is about to do.
     *
     * Both drivers show it - center HTML on its header line, the Panorama menu in its own panel -
     * so it is plain text with no markup, which is what lets either render it.
     */
    MenuBuilder& Subtitle(std::string subtitle)
    {
        _menu.Subtitle = std::move(subtitle);
        return *this;
    }

    /** Append a row built by hand, or by @ref ActionRows. */
    MenuBuilder& Add(MenuItem item)
    {
        _menu.Items.push_back(std::move(item));
        return *this;
    }

    /** Append any row spec: the ones above, and any a plugin writes with the same `ToItem()`.
     *  By value, because `ToItem` consumes it. */
    MenuBuilder& Add(MenuRowSpec auto row) { return Add(std::move(row).ToItem()); }

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

    /** One inert row drawn only if nothing else was added, so a list that filtered down to
     *  nothing is never a dead-end page. Order does not matter; @ref Build applies it. */
    MenuBuilder& EmptyText(std::string label)
    {
        _emptyText = std::move(label);
        return *this;
    }

    /** Finalize and return the built menu. The builder must not be reused after this. */
    std::shared_ptr<Menu> Build()
    {
        if (_menu.Items.empty() && !_emptyText.empty())
            Text(std::move(_emptyText));

        return std::make_shared<Menu>(std::move(_menu));
    }

private:
    Menu _menu;
    std::string _emptyText;
};

template <class T>
MenuItem ChoiceRow<T>::ToItem() &&
{
    // Moved once into a shared state rather than captured by each of the four callbacks below,
    // which would keep that many copies of Choices alive for as long as the menu is open.
    struct State
    {
        std::vector<std::pair<std::string, T>> Choices;
        std::function<void(int slot, const T& value)> Commit;
        std::optional<ChoiceIndex> Bind;
        std::string Label;
        Condition Enabled;
        /** The index the row keeps for itself when the caller supplied no @ref Bind. */
        int Own;

        [[nodiscard]] int Read(int slot) const
        {
            if (Choices.empty())
                return 0;
            const int index = Bind ? Bind->Get(slot) : Own;
            return std::clamp(index, 0, static_cast<int>(Choices.size()) - 1);
        }

        void Write(int slot, int index)
        {
            if (Bind)
                Bind->Set(slot, index);
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

    auto state = std::make_shared<State>(State{.Choices = std::move(Choices),
                                               .Commit = std::move(Commit),
                                               .Bind = std::move(Bind),
                                               .Label = std::move(Label),
                                               .Enabled = std::move(Enabled),
                                               .Own = Index});

    const bool holdsCommit = Apply != ChoiceApply::OnSelect;

    return MenuItem{
        .Describe =
            [state](int slot) {
                const bool has = !state->Choices.empty();
                return MenuRow{
                    .Label = state->Label,
                    .Value = has ? state->Choices[static_cast<std::size_t>(state->Read(slot))].first : std::string{},
                    .Kind = MenuRowKind::Choice,
                    .Enabled = state->Enabled(slot),
                    // Nothing to cycle, so A/D pages instead.
                    .Steppable = has};
            },
        .Activate =
            [state](int slot, MenuSession&) {
                if (!state->Enabled(slot))
                    return;
                // No commit callback: E advances like D, so the row stays interactive for a
                // plain "pick a value" menu with no separate apply step.
                if (state->Commit)
                    state->Apply(slot);
                else
                    (void)state->Step(slot, +1);
            },
        .Step = [state](int slot, int direction) { return state->Enabled(slot) && state->Step(slot, direction); },
        // OnSelect leaves this empty, which is what tells the manager not to hold a commit for
        // the row: nothing applies until the row is activated.
        .Commit = holdsCommit ? std::function<void(int)>([state](int slot) {
            if (state->Enabled(slot))
                state->Apply(slot);
        })
                              : std::function<void(int)>{},
    };
}

}  // namespace VoltMod
