#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>
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
    };

    if (timeoutMs > 0)
    {
        p.TimeoutHandle = Ctx().Scheduler.Delay(timeoutMs, [slot]() { Ctx().ChatInput.CancelCapture(slot); });
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

    auto cb = opt->Cb;
    auto timeoutHandle = opt->TimeoutHandle;

    bool accepted = cb && cb(slot, text);
    if (accepted)
    {
        if (timeoutHandle != 0)
            Ctx().Scheduler.Cancel(timeoutHandle);
        opt.reset();
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
        Ctx().Scheduler.Cancel(opt->TimeoutHandle);

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
