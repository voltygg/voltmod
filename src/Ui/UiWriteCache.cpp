#include <VoltMod/Ui/UiWriteCache.hpp>

namespace VoltMod
{

/** Separates the parts of a key; not legal in a panel id or a variable name. */
static constexpr char kKeySeparator = '\x1f';

bool UiWriteCache::Update(int slot, UiProperty kind, std::string_view panelId, std::string_view name,
                           std::string_view value)
{
    if (!IsValidSlot(slot))
        return false;

    auto& values = _slots[slot].Values;
    const std::string& key = Key(kind, panelId, name);
    if (auto it = values.find(key); it != values.end())
    {
        if (it->second == value)
            return false;

        it->second = value;
        return true;
    }

    values.emplace(key, value);
    return true;
}

bool UiWriteCache::UpdateCapture(int slot, bool enabled)
{
    if (!IsValidSlot(slot))
        return false;

    auto& capture = _slots[slot].Capture;
    if (capture == enabled)
        return false;

    capture = enabled;
    return true;
}

bool UiWriteCache::FirstFailure(int slot)
{
    if (!IsValidSlot(slot))
        return false;

    bool& failed = _slots[slot].Failed;
    if (failed)
        return false;

    failed = true;
    return true;
}

void UiWriteCache::Forget(int slot)
{
    if (!IsValidSlot(slot))
        return;

    SlotState& state = _slots[slot];
    state.Values.clear();
    state.Capture.reset();
}

void UiWriteCache::ForgetAll()
{
    _slots.ResetAll();
}

const std::string& UiWriteCache::Key(UiProperty kind, std::string_view panelId, std::string_view name)
{
    _scratch.assign(1, static_cast<char>(kind));
    _scratch += kKeySeparator;
    _scratch += panelId;
    _scratch += kKeySeparator;
    _scratch += name;
    return _scratch;
}

}  // namespace VoltMod
