#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
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
 * @ref MetamodPlugin::OnPlayerChat calls @ref TryConsume on every incoming
 * `say`/`say_team` message before its own command parsing - when a capture is
 * active for that slot, the message is routed to the registered callback and the
 * chat broadcast is suppressed. A plugin that overrides OnPlayerChat replaces that
 * default and must make the same call, or its menu input rows never complete.
 *
 * The service intentionally does *not* install its own chat hook: VoltMod cannot
 * suppress chat from a `player_say` listener (the broadcast has already
 * happened). Plumbing the consume call through the plugin's existing
 * `Hook_DispatchConCommand` is the only reliable suppression path.
 */
class ChatInput
{
public:
    /** @p scheduler runs the prompt timeouts. @p slots tells the registry when a slot changes
     *  hands, so a prompt cannot outlive the player it was addressed to. Both must outlive it. */
    ChatInput(Scheduler& scheduler, SlotEvents& slots);
    /** Cancels every outstanding timeout, so none can fire into a destroyed registry. */
    ~ChatInput();
    ChatInput(const ChatInput&) = delete;
    ChatInput& operator=(const ChatInput&) = delete;

    /** Validator return: true = accept and clear; false = re-prompt and keep waiting. */
    using Callback = std::function<bool(int slot, std::string_view text)>;

    /**
     * Begin capturing the next chat line from @p slot. If a previous capture is
     * still active, it is replaced (silently cancelled). The capture auto-cancels
     * after @p timeoutMs without input.
     */
    void BeginCapture(int slot, std::string prompt, Callback callback, int timeoutMs = 30000);

    /** True if @p slot currently has a pending prompt. */
    bool IsCapturing(int slot) const;

    /**
     * Route a chat line to the active capture, if any. Returns true when the
     * message was consumed (caller should suppress the chat broadcast).
     */
    bool TryConsume(int slot, std::string_view text);

    /** Cancel without firing the callback. */
    void CancelCapture(int slot);

    /** Cancel only if @p slot still holds the capture with @p id. Used by the timeout, which
     *  must not take out whatever replaced the prompt it was scheduled for. */
    void CancelCaptureById(int slot, uint64_t id);

    /** The active prompt for @p slot, or nullopt if no capture is pending. By value: a capture
     *  can be cancelled or replaced from the same frame that read it. */
    std::optional<std::string> GetPrompt(int slot) const;

private:
    struct Pending
    {
        std::string Prompt;
        Callback Cb;
        uint64_t TimeoutHandle = 0;
        /** Distinguishes this capture from any that replaces it while its own callback runs. */
        uint64_t Id = 0;
    };

    Scheduler& _scheduler;
    std::array<std::optional<Pending>, MaxPlayers> _pending{};
    uint64_t _nextId = 1;
    /** Declared after _pending so it unregisters before the captures its callback cancels. */
    Subscription _slotListener;
};

}  // namespace VoltMod
