#include "Menu/Ui/UiList.hpp"

#include <format>
#include <string>
#include <utility>

namespace VoltMod
{

static constexpr std::string_view kHidden = "Hidden";
static constexpr std::string_view kDisabled = "Disabled";
static constexpr std::string_view kHasValue = "HasValue";
static constexpr std::string_view kHasSteppers = "HasSteppers";

UiList::UiList(UiPanel& panel, std::string_view scopeId, std::string_view prefix, int capacity)
    : _panel(panel), _scopeId(scopeId)
{
    _ids.reserve(static_cast<std::size_t>(capacity < 0 ? 0 : capacity));
    for (int i = 0; i < capacity; ++i)
    {
        std::string row = std::format("{}{}", prefix, i);
        _ids.push_back({.Row = row,
                        .Btn = row + "_btn",
                        .Label = row + "_label",
                        .Value = row + "_value",
                        .Dec = row + "_dec",
                        .Inc = row + "_inc"});
    }
}

void UiList::Set(int slot, int index, const UiRow& row)
{
    if (index < 0 || index >= Capacity() || !IsValidSlot(slot))
        return;

    Bind();

    const RowIds& ids = _ids[static_cast<std::size_t>(index)];

    // Every write below discards its Status: the panel logs the first failure for a slot itself,
    // and a redraw has no better answer than to carry on and try again next frame.
    //
    // Variables on the one scope panel; the labels reading them carry no ids.
    (void)_panel.Text(slot, _scopeId, ids.Label, row.Label);
    (void)_panel.Text(slot, _scopeId, ids.Value, row.Value);

    // One modifier at a time: the row keeps whatever it was given last until something replaces
    // it, so the old one has to come off explicitly rather than being left on underneath.
    std::vector<std::string>& carried = _modifiers[slot];
    carried.resize(_ids.size());
    std::string& previous = carried[static_cast<std::size_t>(index)];
    if (previous != row.Modifier)
    {
        if (!previous.empty())
            (void)_panel.Class(slot, ids.Row, previous, false);
        previous.assign(row.Modifier);
    }
    if (!row.Modifier.empty())
        (void)_panel.Class(slot, ids.Row, row.Modifier, true);

    (void)_panel.Class(slot, ids.Row, kHidden, false);
    (void)_panel.Class(slot, ids.Row, kDisabled, !row.Enabled);
    (void)_panel.Class(slot, ids.Row, kHasValue, !row.Value.empty());
    (void)_panel.Class(slot, ids.Row, kHasSteppers, row.Steppers);
}

void UiList::HideFrom(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    for (int i = index < 0 ? 0 : index; i < Capacity(); ++i)
        (void)_panel.Class(slot, _ids[static_cast<std::size_t>(i)].Row, kHidden, true);
}

void UiList::Bind()
{
    if (!_clicks.Empty() || _ids.empty())
        return;

    for (int i = 0; i < Capacity(); ++i)
    {
        const RowIds& ids = _ids[static_cast<std::size_t>(i)];
        _clicks.On(_panel.Button(ids.Btn), [this, i](int slot) { Pressed.Raise(slot, i); });
        _clicks.On(_panel.Button(ids.Dec), [this, i](int slot) { Stepped.Raise(slot, i, -1); });
        _clicks.On(_panel.Button(ids.Inc), [this, i](int slot) { Stepped.Raise(slot, i, +1); });
    }
}

}  // namespace VoltMod
