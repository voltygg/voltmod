#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace VoltMod
{

/** Fixed or dynamically evaluated enabled state. */
class EnabledCondition
{
public:
    EnabledCondition() = default;
    EnabledCondition(bool value) : _fixed(value) {}
    template <std::predicate<int> Predicate>
        requires(!std::same_as<std::remove_cvref_t<Predicate>, EnabledCondition>)
    EnabledCondition(Predicate predicate) : _predicate(std::move(predicate))
    {}

    [[nodiscard]] bool operator()(int slot) const { return _predicate ? _predicate(slot) : _fixed; }

private:
    std::function<bool(int slot)> _predicate;
    bool _fixed = true;
};

/** A row spec consumable as a @ref MenuItem. */
template <class T>
concept MenuRowSpec = requires(T row) {
    { std::move(row).ToItem() } -> std::same_as<MenuItem>;
};

/** A plain action row. @ref Activate runs on E or a click. */
struct ButtonRow
{
    std::string Label;
    std::function<void(int slot)> Activate;
    EnabledCondition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** A boolean row drawn as a switch. E and A/D run @ref Flip. */
struct ToggleRow
{
    std::string Label;
    std::function<bool(int slot)> Get;
    std::function<void(int slot)> Flip;
    EnabledCondition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** When a @ref ChoiceRow runs its `Commit`. */
enum class ChoiceApply
{
    /** About 400 ms after the last step, so a burst of presses applies once. */
    AfterStep,
    /** Only on E or a click; stepping just changes what the row shows. */
    OnActivate
};

/** External storage for a @ref ChoiceRow selection. */
struct ChoiceIndex
{
    std::function<int(int slot)> Get;
    std::function<void(int slot, int index)> Set;
};

/** A labeled choice list. A/D wraps through the values. */
template <class T>
struct ChoiceRow
{
    std::string Label;
    std::vector<std::pair<std::string, T>> Choices;
    /** Runs on E, or shortly after the last step unless @ref Apply says otherwise. */
    std::function<void(int slot, const T& value)> Commit;
    /** Optional external home for the selection; unset keeps it in the row. */
    std::optional<ChoiceIndex> Bind;
    /** Where an unbound row starts. */
    int Index = 0;
    EnabledCondition Enabled;
    ChoiceApply Apply = ChoiceApply::AfterStep;

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
    EnabledCondition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** A link to another menu. @ref Build runs on E and its result is pushed onto the stack. */
struct SubmenuRow
{
    std::string Label;
    std::function<std::shared_ptr<Menu>(int slot)> Build;
    EnabledCondition Enabled;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** A heading or divider. The cursor skips it. */
struct TextRow
{
    std::string Label;

    [[nodiscard]] MenuItem ToItem() &&;
};

/** Fluent builder for menus and row specs. */
class MenuBuilder
{
public:
    explicit MenuBuilder(std::string title) { _menu.Title = std::move(title); }

    /** Sets optional plain-text detail shown with the title. */
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

    /** Appends any compatible row spec. */
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

    /** Sets a text row used only when no other rows were added. */
    MenuBuilder& EmptyText(std::string label)
    {
        _emptyText = std::move(label);
        return *this;
    }

    /** Returns the built menu. The builder must not be reused after this. */
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
    // Share Choices across callbacks so the open menu keeps one copy.
    struct State
    {
        std::vector<std::pair<std::string, T>> Choices;
        std::function<void(int slot, const T& value)> Commit;
        std::optional<ChoiceIndex> Bind;
        std::string Label;
        EnabledCondition Enabled;
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

    const bool holdsCommit = Apply == ChoiceApply::AfterStep;

    return MenuItem{
        .Describe =
            [state](int slot) {
                const bool has = !state->Choices.empty();
                return MenuRow{
                    .Label = state->Label,
                    .Value = has ? state->Choices[static_cast<std::size_t>(state->Read(slot))].first : std::string{},
                    .Kind = MenuRowKind::Choice,
                    .Enabled = state->Enabled(slot),
                    // An empty list cannot step, so A/D pages instead.
                    .Steppable = has};
            },
        .Activate =
            [state](int slot, MenuSurface&) {
                if (!state->Enabled(slot))
                    return;
                // Without a commit callback, E advances like D for a live pick-a-value row.
                if (state->Commit)
                    state->Apply(slot);
                else
                    (void)state->Step(slot, +1);
            },
        .Step = [state](int slot, int direction) { return state->Enabled(slot) && state->Step(slot, direction); },
        // No commit callback tells the stack that OnActivate applies only on activation.
        .Commit = holdsCommit ? std::function<void(int)>([state](int slot) {
            if (state->Enabled(slot))
                state->Apply(slot);
        })
                              : std::function<void(int)>{},
    };
}

}  // namespace VoltMod
