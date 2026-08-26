#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <utility>

namespace VoltMod::Hooks
{

ChatInput::ChatInput(Core::Scheduler& scheduler, Core::SlotEvents& slots)
    : _scheduler(scheduler),
      // SlotEvents fires when a slot is filled as well as emptied; a fresh occupant has no capture
      // pending, so cancelling on both edges covers "left" without a dedicated event.
      _slotListener(slots.Listen([this](int slot) { CancelCapture(slot); }))
{}

ChatInput::~ChatInput()
{
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        CancelCapture(slot);
}

void ChatInput::BeginCapture(int slot, std::string prompt, Callback callback, int timeoutMs)
{
    if (!Core::IsValidSlot(slot) || !callback)
        return;

    CancelCapture(slot);  // drops any existing prompt + scheduled timeout

    Pending p{
        .Prompt = std::move(prompt),
        .Cb = std::move(callback),
        .TimeoutHandle = 0,
        .Id = _nextId++,
    };

    if (timeoutMs > 0)
    {
        // Cancel by capture id, not by slot: the prompt can outlive its player, and cancelling
        // the slot would take out whatever the next occupant had open.
        const uint64_t id = p.Id;
        // Capturing `this` is safe: the scheduler is a sibling service that outlives no longer than
        // this one, and every pending timer is cancelled before a capture is dropped.
        p.TimeoutHandle = _scheduler.Delay(timeoutMs, [this, slot, id]() { CancelCaptureById(slot, id); });
    }

    _pending[slot] = std::move(p);
}

bool ChatInput::IsCapturing(int slot) const
{
    if (!Core::IsValidSlot(slot))
        return false;
    return _pending[slot].has_value();
}

bool ChatInput::TryConsume(int slot, std::string_view text)
{
    if (!Core::IsValidSlot(slot))
        return false;

    auto& opt = _pending[slot];
    if (!opt.has_value())
        return false;

    // Copy before invoking: a menu flow routinely chains prompts, so the callback can replace
    // this very capture (BeginCapture) or drop it (CancelCapture).
    auto cb = opt->Cb;
    const uint64_t id = opt->Id;

    bool accepted = cb && cb(slot, text);

    // Clear only if the capture we just ran is still the one installed. Without the id check a
    // callback that chained a follow-up prompt had it deleted the moment it returned.
    if (accepted)
    {
        auto& current = _pending[slot];
        if (current.has_value() && current->Id == id)
        {
            if (current->TimeoutHandle != 0)
                _scheduler.Cancel(current->TimeoutHandle);
            current.reset();
        }
    }
    // Either way we suppress the chat broadcast - the player typed a value, not a chat message.
    return true;
}

void ChatInput::CancelCaptureById(int slot, uint64_t id)
{
    if (!Core::IsValidSlot(slot))
        return;

    const auto& opt = _pending[slot];
    if (opt.has_value() && opt->Id == id)
        CancelCapture(slot);
}

void ChatInput::CancelCapture(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& opt = _pending[slot];
    if (!opt.has_value())
        return;

    if (opt->TimeoutHandle != 0)
        _scheduler.Cancel(opt->TimeoutHandle);

    opt.reset();
}

std::optional<std::string> ChatInput::GetPrompt(int slot) const
{
    if (!Core::IsValidSlot(slot))
        return std::nullopt;
    const auto& opt = _pending[slot];
    if (!opt)
        return std::nullopt;
    return opt->Prompt;
}

}  // namespace VoltMod::Hooks
