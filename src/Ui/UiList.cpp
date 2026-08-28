#include <VoltMod/Ui/UiList.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

/** The dialog variable both text panels read; `text="{s:text}"` in the markup. */
static constexpr std::string_view kTextVariable = "text";

static constexpr std::string_view kHidden = "Hidden";
static constexpr std::string_view kDisabled = "Disabled";
static constexpr std::string_view kHasValue = "HasValue";
static constexpr std::string_view kHasSteppers = "HasSteppers";

UiList::UiList(UiLayout& layout, std::string_view prefix, int capacity) : _layout(layout)
{
    _ids.reserve(static_cast<std::size_t>(capacity < 0 ? 0 : capacity));
    for (int i = 0; i < capacity; ++i)
    {
        std::string row = std::format("{}{}", prefix, i);
        _ids.push_back(
            {.Row = row, .Label = row + "_label", .Value = row + "_value", .Dec = row + "_dec", .Inc = row + "_inc"});
    }
}

void UiList::Set(int slot, int index, const UiRow& row)
{
    if (index < 0 || index >= Capacity() || !IsValidSlot(slot))
        return;

    Bind();

    const RowIds& ids = _ids[static_cast<std::size_t>(index)];
    _layout.Text(slot, ids.Label, kTextVariable, row.Label);
    _layout.Text(slot, ids.Value, kTextVariable, row.Value);

    // One modifier at a time: the row keeps whatever it was given last until something replaces
    // it, so the old one has to come off explicitly rather than being left on underneath.
    std::vector<std::string>& carried = _modifiers[slot];
    carried.resize(_ids.size());
    std::string& previous = carried[static_cast<std::size_t>(index)];
    if (previous != row.Modifier)
    {
        if (!previous.empty())
            _layout.Class(slot, ids.Row, previous, false);
        previous.assign(row.Modifier);
    }
    if (!row.Modifier.empty())
        _layout.Class(slot, ids.Row, row.Modifier, true);

    _layout.Class(slot, ids.Row, kHidden, false);
    _layout.Class(slot, ids.Row, kDisabled, !row.Enabled);
    _layout.Class(slot, ids.Row, kHasValue, !row.Value.empty());
    _layout.Class(slot, ids.Row, kHasSteppers, row.Steppers);
}

void UiList::HideFrom(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    for (int i = index < 0 ? 0 : index; i < Capacity(); ++i)
        _layout.Class(slot, _ids[static_cast<std::size_t>(i)].Row, kHidden, true);
}

void UiList::Bind()
{
    if (!_clicks.empty() || _ids.empty())
        return;

    _clicks.reserve(_ids.size() * 3);
    for (int i = 0; i < Capacity(); ++i)
    {
        const RowIds& ids = _ids[static_cast<std::size_t>(i)];
        _clicks.push_back(_layout.OnClick(ids.Row, [this, i](int slot) { Pressed.Raise(slot, i); }));
        _clicks.push_back(_layout.OnClick(ids.Dec, [this, i](int slot) { Stepped.Raise(slot, i, -1); }));
        _clicks.push_back(_layout.OnClick(ids.Inc, [this, i](int slot) { Stepped.Raise(slot, i, +1); }));
    }
}

}  // namespace VoltMod
