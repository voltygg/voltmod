#include <VoltMod/Menu/MenuBuilder.hpp>
#include <utility>

namespace VoltMod
{

// Every spec is copied into its callbacks by value: a row outlives the spec value the caller
// wrote it from, and a menu is routinely built from locals inside a factory lambda.

MenuItem ButtonRow::ToItem() const
{
    return MenuItem{
        .Describe = [label = Label, enabled = Enabled](
                        int) { return MenuRow{.Label = label, .Kind = MenuRowKind::Button, .Enabled = enabled}; },
        .Activate =
            [activate = Activate, enabled = Enabled](int slot, MenuSession&) {
                if (enabled && activate)
                    activate(slot);
            },
    };
}

MenuItem ToggleRow::ToItem() const
{
    return MenuItem{
        .Describe =
            [label = Label, on = On, off = Off, get = Get, enabled = Enabled](int slot) {
                const bool state = get && get(slot);
                return MenuRow{.Label = label,
                               .Value = state ? on : off,
                               .Kind = MenuRowKind::Toggle,
                               .Enabled = enabled,
                               .Steppable = true,
                               .State = state};
            },
        .Activate =
            [flip = Flip, enabled = Enabled](int slot, MenuSession&) {
                if (enabled && flip)
                    flip(slot);
            },
        .Step =
            [flip = Flip, enabled = Enabled](int slot, int) {
                if (!enabled || !flip)
                    return false;
                flip(slot);
                return true;
            },
    };
}

MenuItem InputRow::ToItem() const
{
    return MenuItem{
        .Describe =
            [label = Label, get = Get, enabled = Enabled](int slot) {
                std::string value = get ? get(slot) : std::string{};
                // An unset value still needs to look like a field waiting for one.
                return MenuRow{.Label = label,
                               .Value = value.empty() ? "…" : std::move(value),
                               .Kind = MenuRowKind::Input,
                               .Enabled = enabled};
            },
        .Activate =
            [prompt = Prompt, set = Set, maxLength = MaxLength, enabled = Enabled](int slot, MenuSession& session) {
                if (!enabled)
                    return;
                session.Prompt(slot, prompt, [set, maxLength](int s, std::string_view text) {
                    // Over-long text re-prompts rather than reaching the setter: a chat line is
                    // whatever the player typed, and the row said how much of it it wants.
                    if (maxLength > 0 && static_cast<int>(text.size()) > maxLength)
                        return false;
                    return set ? set(s, text) : true;
                });
            },
    };
}

MenuItem SubmenuRow::ToItem() const
{
    return MenuItem{
        .Describe = [label = Label, enabled = Enabled](
                        int) { return MenuRow{.Label = label, .Kind = MenuRowKind::Submenu, .Enabled = enabled}; },
        .Activate =
            [build = Build, enabled = Enabled](int slot, MenuSession& session) {
                if (!enabled || !build)
                    return;
                if (auto submenu = build(slot))
                    session.Open(slot, std::move(submenu));
            },
    };
}

MenuItem TextRow::ToItem() const
{
    return MenuItem{
        .Describe =
            [label = Label](int) { return MenuRow{.Label = label, .Kind = MenuRowKind::Text, .Selectable = false}; },
    };
}

}  // namespace VoltMod
