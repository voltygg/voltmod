#include "Ui/SentWrites.hpp"

namespace VoltMod
{

/** Separates the parts of a key; not legal in a panel id or a variable name. */
static constexpr char kKeySeparator = '\x1f';

SentWrites::SlotState* SentWrites::At(int slot)
{
    if (slot == EveryoneSlot)
        return &_shared;
    return IsValidSlot(slot) ? &_slots[slot] : nullptr;
}

bool SentWrites::Changed(int slot, WriteKind kind, std::string_view panelId, std::string_view name,
                         std::string_view value)
{
    SlotState* state = At(slot);
    if (!state)
        return false;

    auto& values = state->Values;
    const std::string& key = KeyFor(kind, panelId, name);
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

bool SentWrites::CaptureChanged(int slot, bool enabled)
{
    SlotState* state = At(slot);
    if (!state)
        return false;

    if (state->Capture == enabled)
        return false;

    state->Capture = enabled;
    return true;
}

bool SentWrites::IsFirstFailure(int slot)
{
    SlotState* state = At(slot);
    if (!state || state->Failed)
        return false;

    state->Failed = true;
    return true;
}

void SentWrites::Forget(int slot)
{
    SlotState* state = At(slot);
    if (!state)
        return;

    state->Values.clear();
    state->Capture.reset();
}

void SentWrites::ForgetAll()
{
    _slots.ResetAll();
    _shared = {};
}

const std::string& SentWrites::KeyFor(WriteKind kind, std::string_view panelId, std::string_view name)
{
    _scratch.assign(1, static_cast<char>(kind));
    _scratch += kKeySeparator;
    _scratch += panelId;
    _scratch += kKeySeparator;
    _scratch += name;
    return _scratch;
}

}  // namespace VoltMod
