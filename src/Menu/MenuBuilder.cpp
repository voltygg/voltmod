#include <VoltMod/Menu/MenuBuilder.hpp>
#include <utility>

namespace VoltMod
{

/** Shared description callback for menu specs. */
static std::function<MenuRow(int)> Describer(std::string label, MenuRowKind kind, EnabledCondition enabled,
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
static std::function<void(int, MenuSession&)> Gated(EnabledCondition enabled,
                                                    std::function<void(int, MenuSession&)> action)
{
    return [enabled = std::move(enabled), action = std::move(action)](int slot, MenuSession& session) {
        if (action && enabled(slot))
            action(slot, session);
    };
}

MenuItem ButtonRow::ToItem() &&
{
    return MenuItem{
        .Describe = Describer(std::move(Label), MenuRowKind::Button, Enabled),
        .Activate = Gated(std::move(Enabled),
                          [activate = std::move(Activate)](int slot, MenuSession&) {
                              if (activate)
                                  activate(slot);
                          }),
    };
}

MenuItem ToggleRow::ToItem() &&
{
    // ActiveMenus::Describe supplies the localized state text.
    return MenuItem{
        .Describe = Describer(std::move(Label), MenuRowKind::Toggle, Enabled,
                              [get = Get](int slot, MenuRow& row) {
                                  row.Steppable = true;
                                  row.State = get && get(slot);
                              }),
        .Activate = Gated(Enabled,
                          [flip = Flip](int slot, MenuSession&) {
                              if (flip)
                                  flip(slot);
                          }),
        .Step =
            [flip = std::move(Flip), enabled = std::move(Enabled)](int slot, int) {
                if (!flip || !enabled(slot))
                    return false;
                flip(slot);
                return true;
            },
    };
}

MenuItem InputRow::ToItem() &&
{
    return MenuItem{
        .Describe = Describer(std::move(Label), MenuRowKind::Input, Enabled,
                              [get = std::move(Get)](int slot, MenuRow& row) {
                                  // An unset value still needs to look like a field waiting for one.
                                  std::string value = get ? get(slot) : std::string{};
                                  row.Value = value.empty() ? "…" : std::move(value);
                              }),
        .Activate = Gated(
            std::move(Enabled),
            [prompt = std::move(Prompt), set = std::move(Set), maxLength = MaxLength](int slot, MenuSession& session) {
                session.Prompt(slot, prompt, [set, maxLength](int s, std::string_view text) {
                    // Reject over-long client input before calling the setter.
                    if (maxLength > 0 && static_cast<int>(text.size()) > maxLength)
                        return false;
                    return set ? set(s, text) : true;
                });
            }),
    };
}

MenuItem SubmenuRow::ToItem() &&
{
    return MenuItem{
        .Describe = Describer(std::move(Label), MenuRowKind::Submenu, Enabled),
        .Activate = Gated(std::move(Enabled),
                          [build = std::move(Build)](int slot, MenuSession& session) {
                              if (!build)
                                  return;
                              if (auto submenu = build(slot))
                                  session.Open(slot, std::move(submenu));
                          }),
    };
}

MenuItem TextRow::ToItem() &&
{
    return MenuItem{.Describe = Describer(std::move(Label), MenuRowKind::Text, true)};
}

}  // namespace VoltMod
