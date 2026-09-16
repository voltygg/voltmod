#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <utility>

namespace VoltMod
{

ChatInput::ChatInput(Scheduler& scheduler, SlotEvents& slots)
    : _scheduler(scheduler),
      // SlotEvents fires when a slot is filled as well as emptied; a fresh occupant has no capture
      // pending, so cancelling on both edges covers "left" without a dedicated event.
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

    CancelCapture(slot);  // drops any existing prompt + scheduled timeout

    Pending p{
        .Prompt = std::move(prompt),
        .Cb = std::move(callback),
        .Id = _nextId++,
    };

    if (timeoutMs > 0)
    {
        // Cancel by capture id, not by slot: the prompt can outlive its player, and cancelling
        // the slot would take out whatever the next occupant had open.
        const uint64_t id = p.Id;
        // Capturing `this` is safe: the timer lives inside the capture, so dropping the capture
        // cancels it before this registry can go away.
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

    // Taken before invoking, so a surface that redraws from the callback already sees no pending
    // prompt and stops rendering one. It also leaves the callback free to chain its own capture.
    Pending pending = std::move(*opt);
    opt.reset();  // takes its pending timeout with it

    const bool accepted = pending.Cb && pending.Cb(slot, text);

    // A rejected value keeps the player at the same prompt, so put it back - unless the callback
    // has already moved them on to a different one.
    if (!accepted && !_pending[slot].has_value())
        _pending[slot] = std::move(pending);

    // Either way we suppress the chat broadcast - the player typed a value, not a chat message.
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

    _pending[slot].reset();  // takes its pending timeout with it
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
