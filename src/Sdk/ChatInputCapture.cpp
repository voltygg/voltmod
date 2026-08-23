#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <utility>

namespace CS2Kit::Sdk
{

void ChatInputCapture::BeginCapture(int slot, std::string prompt, Callback callback, int timeoutMs)
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
        p.TimeoutHandle = CS2Kit::Detail::Rt().Scheduler.Delay(
            timeoutMs, [slot]() { CS2Kit::Detail::Rt().ChatInput.CancelCapture(slot); });
    }

    _pending[slot] = std::move(p);
}

bool ChatInputCapture::IsCapturing(int slot) const
{
    if (!Core::IsValidSlot(slot))
        return false;
    return _pending[slot].has_value();
}

bool ChatInputCapture::TryConsume(int slot, std::string_view text)
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
                CS2Kit::Detail::Rt().Scheduler.Cancel(current->TimeoutHandle);
            current.reset();
        }
    }
    // Either way we suppress the chat broadcast - the player typed a value, not a chat message.
    return true;
}

void ChatInputCapture::CancelCapture(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& opt = _pending[slot];
    if (!opt.has_value())
        return;

    if (opt->TimeoutHandle != 0)
        CS2Kit::Detail::Rt().Scheduler.Cancel(opt->TimeoutHandle);

    opt.reset();
}

const std::string* ChatInputCapture::GetPrompt(int slot) const
{
    if (!Core::IsValidSlot(slot))
        return nullptr;
    const auto& opt = _pending[slot];
    return opt.has_value() ? &opt->Prompt : nullptr;
}

void ChatInputCapture::OnPlayerDisconnect(int slot)
{
    CancelCapture(slot);
}

}  // namespace CS2Kit::Sdk
