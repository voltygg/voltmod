#pragma once

#include <VoltMod/Core/Text/Labeled.hpp>
#include <VoltMod/Menu/Menu.hpp>
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

    bool operator()(int slot) const { return _predicate ? _predicate(slot) : _fixed; }

private:
    std::function<bool(int slot)> _predicate;
    bool _fixed = true;
};

/** A row spec consumable as a @ref MenuItem. */
template <class T>
concept MenuRowSpec = requires(const T& row) {
    { row.ToItem() } -> std::same_as<MenuItem>;
};

/** A plain action row. @ref Activate runs on E or a click. */
struct ButtonRow
{
    std::string Label;
    std::function<void(int slot)> Activate;
    EnabledCondition Enabled;

    MenuItem ToItem() const;
};

/** A boolean row drawn as a switch. E and A/D run @ref Flip. */
struct ToggleRow
{
    std::string Label;
    std::function<bool(int slot)> Get;
    std::function<void(int slot)> Flip;
    EnabledCondition Enabled;

    MenuItem ToItem() const;
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

namespace Internal
{
/** @ref ChoiceRow's row with its values reduced to indices; build rows with ChoiceRow instead. */
MenuItem ChoiceItem(std::string label, std::vector<std::string> choices,
                    std::function<void(int slot, int index)> commit, std::optional<ChoiceIndex> bind, int index,
                    EnabledCondition enabled, ChoiceApply apply);
}  // namespace Internal

/** A labeled choice list. A/D wraps through the values. */
template <class T>
struct ChoiceRow
{
    std::string Label;
    std::vector<Labeled<T>> Choices;
    /** Runs on E, or shortly after the last step unless @ref Apply says otherwise. */
    std::function<void(int slot, const T& value)> Commit;
    /** Optional external home for the selection; unset keeps it in the row. */
    std::optional<ChoiceIndex> Bind;
    /** Where an unbound row starts. */
    int Index = 0;
    EnabledCondition Enabled;
    ChoiceApply Apply = ChoiceApply::AfterStep;

    MenuItem ToItem() const
    {
        std::vector<std::string> labels;
        std::vector<T> values;
        for (const auto& [label, value] : Choices)
        {
            labels.push_back(label);
            values.push_back(value);
        }

        std::function<void(int slot, int index)> commitIndex;
        if (Commit)
        {
            commitIndex = [values = std::move(values), commit = Commit](int slot, int index) {
                commit(slot, values[static_cast<std::size_t>(index)]);
            };
        }
        return Internal::ChoiceItem(Label, std::move(labels), std::move(commitIndex), Bind, Index, Enabled, Apply);
    }
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

    MenuItem ToItem() const;
};

/** A link to another menu. @ref Build runs on E and its result is pushed onto the stack. */
struct SubmenuRow
{
    std::string Label;
    std::function<std::shared_ptr<Menu>(int slot)> Build;
    EnabledCondition Enabled;
    /** Reported as @ref MenuRow::Icon. */
    std::string Icon;

    MenuItem ToItem() const;
};

/** A heading or divider. The cursor skips it. */
struct TextRow
{
    std::string Label;

    MenuItem ToItem() const;
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
    MenuBuilder& Add(const MenuRowSpec auto& row) { return Add(row.ToItem()); }

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

}  // namespace VoltMod
