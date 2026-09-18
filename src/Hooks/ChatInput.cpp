#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <utility>

namespace VoltMod
{

ChatInput::ChatInput(Scheduler& scheduler, SlotEvents& slots)
    : _scheduler(scheduler),
      // Slot changes cancel captures so a new occupant cannot inherit the previous occupant's prompt.
      _slotListener(slots.Changed += [this](int slot) { CancelCapture(slot); })
{}

ChatInput::~ChatInput()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        CancelCapture(slot);
}

void ChatInput::BeginCapture(int slot, std::string prompt, Callback callback, int timeoutMs)
{
    if (!IsValidSlot(slot) || !callback)
        return;

    CancelCapture(slot);

    Pending p{
        .Prompt = std::move(prompt),
        .Cb = std::move(callback),
        .Id = _nextId++,
    };

    if (timeoutMs > 0)
    {
        // Match by capture ID so a stale timeout cannot cancel a newer capture in the same slot.
        const uint64_t id = p.Id;
        // Each capture owns its timer, so clearing the capture prevents callbacks into this registry.
        p.Timeout = _scheduler.Delay(timeoutMs, [this, slot, id]() { CancelCaptureById(slot, id); });
    }

    _pending[slot] = std::move(p);
}

bool ChatInput::IsCapturing(int slot) const
{
    if (!IsValidSlot(slot))
        return false;
    return _pending[slot].has_value();
}

bool ChatInput::TryConsume(int slot, std::string_view text)
{
    if (!IsValidSlot(slot))
        return false;

    auto& opt = _pending[slot];
    if (!opt.has_value())
        return false;

    // Move the capture out before the callback so redraws see no prompt and callbacks can start another capture.
    Pending pending = std::move(*opt);
    opt.reset();

    const bool accepted = pending.Cb && pending.Cb(slot, text);

    // Restore rejected input unless the callback already installed a replacement capture.
    if (!accepted && !_pending[slot].has_value())
        _pending[slot] = std::move(pending);

    // Captured input is never forwarded as chat, even when rejected.
    return true;
}

void ChatInput::CancelCaptureById(int slot, uint64_t id)
{
    if (!IsValidSlot(slot))
        return;

    const auto& opt = _pending[slot];
    if (opt.has_value() && opt->Id == id)
        CancelCapture(slot);
}

void ChatInput::CancelCapture(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _pending[slot].reset();  // Dropping the capture also cancels its timeout.
}

std::optional<std::string> ChatInput::GetPrompt(int slot) const
{
    if (!IsValidSlot(slot))
        return std::nullopt;
    const auto& opt = _pending[slot];
    if (!opt)
        return std::nullopt;
    return opt->Prompt;
}

}  // namespace VoltMod
