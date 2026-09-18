#pragma once

#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Per-player pending-prompt registry for menu free-text input.
 *
 * @ref Plugin::OnPlayerChat calls @ref TryConsume for every `say`/`say_team`
 * message before command parsing. An active capture routes the message to its
 * callback and suppresses the chat broadcast. Overrides must make the same call
 * or menu text input will not complete.
 *
 * This service does not install a separate chat hook: `player_say` runs after the
 * broadcast. The consume call must remain in `Hook_DispatchConCommand`.
 */
class ChatInput
{
public:
    /** Uses @p scheduler for timeouts and @p slots to clear recycled slots. Both must outlive this object. */
    ChatInput(Scheduler& scheduler, SlotEvents& slots);
    /** Cancels outstanding captures and their timeouts before the registry is destroyed. */
    ~ChatInput();
    ChatInput(const ChatInput&) = delete;
    ChatInput& operator=(const ChatInput&) = delete;

    /** The callback returns true to accept and clear input, or false to keep capturing. */
    using Callback = std::function<bool(int slot, std::string_view text)>;

    /**
     * Begin capturing the next chat line from @p slot. Replaces and cancels any
     * existing capture. A positive @p timeoutMs cancels the capture without input.
     */
    void BeginCapture(int slot, std::string prompt, Callback callback, int timeoutMs = 60000);

    bool IsCapturing(int slot) const;

    /**
     * Route a chat line to the active capture, if any. Returns true when the
     * message was consumed, so the caller must suppress the chat broadcast.
     * A rejected value restores the capture unless the callback installed a replacement.
     */
    bool TryConsume(int slot, std::string_view text);

    /** Cancel without firing the callback. */
    void CancelCapture(int slot);

    /** Returns a copy of the active prompt, or nullopt. The copy remains valid if the capture changes. */
    std::optional<std::string> GetPrompt(int slot) const;

private:
    /** Cancel only if @p slot still holds @p id, so a timeout cannot cancel its replacement. */
    void CancelCaptureById(int slot, uint64_t id);

    struct Pending
    {
        std::string Prompt;
        Callback Cb;
        Subscription Timeout;
        uint64_t Id = 0;  // Identifies the capture associated with its timeout.
    };

    Scheduler& _scheduler;
    std::array<std::optional<Pending>, MaxPlayers> _pending{};
    uint64_t _nextId = 1;
    /** Declared after _pending so it unsubscribes before _pending is destroyed. */
    Subscription _slotListener;
};

}  // namespace VoltMod
