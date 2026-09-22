#include <VoltMod/Menu/MenuBuilder.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

namespace VoltMod
{

/** Shared description callback for menu specs. */
static std::function<MenuRow(int)> DescribeRow(std::string label, MenuRowKind kind, EnabledCondition enabled,
                                               std::function<void(int slot, MenuRow& row)> live = {})
{
    return [label = std::move(label), kind, enabled = std::move(enabled), live = std::move(live)](int slot) {
        MenuRow row{.Label = label,
                    .Kind = kind,
                    .Enabled = enabled(slot),
                    // Only a caption refuses the cursor.
                    .Selectable = kind != MenuRowKind::Text};
        if (live)
            live(slot, row);
        return row;
    };
}

/** @p action, run only while @p enabled takes the slot. */
static std::function<void(int, MenuSurface&)> WhenEnabled(EnabledCondition enabled,
                                                          std::function<void(int, MenuSurface&)> action)
{
    return [enabled = std::move(enabled), action = std::move(action)](int slot, MenuSurface& surface) {
        if (action && enabled(slot))
            action(slot, surface);
    };
}

MenuItem ButtonRow::ToItem() const
{
    return MenuItem{
        .Describe = DescribeRow(Label, MenuRowKind::Button, Enabled),
        .Activate = WhenEnabled(Enabled,
                                [activate = Activate](int slot, MenuSurface&) {
                                    if (activate)
                                        activate(slot);
                                }),
    };
}

MenuItem ToggleRow::ToItem() const
{
    // MenuStack::Describe supplies the localized state text.
    return MenuItem{
        .Describe = DescribeRow(Label, MenuRowKind::Toggle, Enabled,
                                [get = Get](int slot, MenuRow& row) {
                                    row.Steppable = true;
                                    row.State = get && get(slot);
                                }),
        .Activate = WhenEnabled(Enabled,
                                [flip = Flip](int slot, MenuSurface&) {
                                    if (flip)
                                        flip(slot);
                                }),
        .Step =
            [flip = Flip, enabled = Enabled](int slot, int) {
                if (!flip || !enabled(slot))
                    return false;
                flip(slot);
                return true;
            },
    };
}

MenuItem InputRow::ToItem() const
{
    return MenuItem{
        .Describe = DescribeRow(Label, MenuRowKind::Input, Enabled,
                                [get = Get](int slot, MenuRow& row) {
                                    // An unset value still needs to look like a field waiting for one.
                                    std::string value = get ? get(slot) : std::string{};
                                    row.Value = value.empty() ? "…" : std::move(value);
                                }),
        .Activate = WhenEnabled(Enabled,
                                [prompt = Prompt, set = Set, maxLength = MaxLength](int slot, MenuSurface& surface) {
                                    surface.Prompt(slot, prompt, [set, maxLength](int s, std::string_view text) {
                                        // Reject over-long client input before calling the setter.
                                        if (maxLength > 0 && static_cast<int>(text.size()) > maxLength)
                                            return false;
                                        return set ? set(s, text) : true;
                                    });
                                }),
    };
}

MenuItem SubmenuRow::ToItem() const
{
    return MenuItem{
        .Describe =
            DescribeRow(Label, MenuRowKind::Submenu, Enabled, [icon = Icon](int, MenuRow& row) { row.Icon = icon; }),
        .Activate = WhenEnabled(Enabled,
                                [build = Build](int slot, MenuSurface& surface) {
                                    if (!build)
                                        return;
                                    if (auto submenu = build(slot))
                                        surface.Open(slot, std::move(submenu));
                                }),
    };
}

MenuItem TextRow::ToItem() const
{
    return MenuItem{.Describe = DescribeRow(Label, MenuRowKind::Text, true)};
}

/** One shared copy, so every callback of the open menu sees the same selection. */
struct ChoiceState
{
    std::vector<std::string> Choices;
    std::function<void(int slot, int index)> Commit;
    std::optional<ChoiceIndex> Bind;
    int Own;

    int Selected(int slot) const
    {
        if (Choices.empty())
            return 0;
        const int index = Bind ? Bind->Get(slot) : Own;
        return std::clamp(index, 0, static_cast<int>(Choices.size()) - 1);
    }

    void Select(int slot, int index)
    {
        if (Bind)
            Bind->Set(slot, index);
        else
            Own = index;
    }

    void Apply(int slot) const
    {
        if (Commit && !Choices.empty())
            Commit(slot, Selected(slot));
    }

    bool Step(int slot, int direction)
    {
        if (Choices.empty())
            return false;
        Select(slot, WrapIndex(Selected(slot) + direction, static_cast<int>(Choices.size())));
        return true;
    }
};

MenuItem Internal::ChoiceItem(std::string label, std::vector<std::string> choices,
                              std::function<void(int slot, int index)> commit, std::optional<ChoiceIndex> bind,
                              int index, EnabledCondition enabled, ChoiceApply apply)
{
    auto state = std::make_shared<ChoiceState>(
        ChoiceState{.Choices = std::move(choices), .Commit = std::move(commit), .Bind = std::move(bind), .Own = index});

    MenuItem item{
        .Describe = DescribeRow(std::move(label), MenuRowKind::Choice, enabled,
                                [state](int slot, MenuRow& row) {
                                    // An empty list cannot step, so A/D pages instead.
                                    if (state->Choices.empty())
                                        return;
                                    row.Value = state->Choices[static_cast<std::size_t>(state->Selected(slot))];
                                    row.Steppable = true;
                                }),
        .Activate = WhenEnabled(enabled,
                                [state](int slot, MenuSurface&) {
                                    // Without a commit callback, E advances like D for a live pick-a-value row.
                                    if (state->Commit)
                                        state->Apply(slot);
                                    else
                                        state->Step(slot, +1);
                                }),
        .Step = [state, enabled](int slot, int direction) { return enabled(slot) && state->Step(slot, direction); },
    };

    // Left empty for OnActivate: a row without Commit is applied only when activated.
    if (apply == ChoiceApply::AfterStep)
    {
        item.Commit = [state, enabled](int slot) {
            if (enabled(slot))
                state->Apply(slot);
        };
    }
    return item;
}

}  // namespace VoltMod
