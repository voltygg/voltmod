#pragma once

#include "Ui/UiClickRouting.hpp"
#include "Ui/UiWriteCache.hpp"

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Everything a @ref UiPanel keeps between calls.
 *
 * Held by `shared_ptr` from the panel: moving a panel moves the pointer, so the events handlers
 * hold Subscriptions to - and the entity ref those handlers filter on - stay where they are.
 * Nothing outside `src/` names this type; @ref UiPanel is the whole surface.
 */
struct UiPanelState
{
    /**
     * Pointers rather than references because an empty panel has no engine behind it and must
     * still answer every call. @p slots may be null, in which case nothing resets the write cache
     * when a slot changes hands. Everything non-null must outlive the panel, which the Runtime's
     * declaration order gives.
     */
    explicit UiPanelState(EntitySystem* entities = nullptr, EntityOps* ops = nullptr, SlotEvents* slots = nullptr,
                          Event<const UiClick&>* allClicks = nullptr, std::string layout = {},
                          std::string resource = {});

    UiPanelState(const UiPanelState&) = delete;
    UiPanelState& operator=(const UiPanelState&) = delete;

    /** Spawn a fresh entity, dropping the old one and everything remembered about it. */
    Status Spawn();

    /** Remove the entity and forget what every player was told about it. Idempotent. */
    void Remove();

    /** Whether the entity exists and carries per-player state for @p slot. */
    [[nodiscard]] bool Covers(int slot) const;

    /** Pass @p status through, and on a failure drop what the cache just recorded so the next
     *  frame retries - saying why once per generation rather than once per frame. */
    Status RecordWrite(int slot, Status status, std::string_view what);

    /** The event for one Button id, created on first use. */
    Event<int>& Button(std::string_view id);

    /** @ref Event::Lifecycle shared by @ref Clicked and every @ref Buttons entry: one subscription
     *  to @ref CustomUi::Clicked, counted across all of them. False when the hook refused, which
     *  refuses the subscription that asked and leaves a later one free to try again. */
    bool OnFirstSubscriber();
    void OnLastSubscriber();

    EntitySystem* Entities = nullptr;
    EntityOps* Ops = nullptr;
    /** @ref CustomUi::Clicked: every press, filtered down to this layout by @ref ClickListener. */
    Event<const UiClick&>* AllClicks = nullptr;

    /** The layout as it was named, and the resource name that goes on the entity. */
    std::string Layout;
    std::string Resource;

    /** The entity handlers filter on. Cleared by @ref Remove and replaced by @ref Spawn, which is
     *  what makes a subscription survive a re-spawn. */
    EntityRef CurrentEntity;

    /** A player has connected or disconnected since the last spawn. The per-player state count is
     *  fixed when the entity spawns, so a slot that connected later is only reachable through a new
     *  entity - and re-spawning on any other trigger would retry a hopeless spawn every frame. */
    bool PlayersChanged = true;

    UiWriteCache Cache;
    Event<const UiClick&> Clicked;
    Internal::UiButtonEvents Buttons;

    /** Live subscriptions across @ref Clicked and @ref Buttons, so @ref ClickListener is taken
     *  once however many handlers there are. */
    int Subscribers = 0;

    /** Declared last: their handlers touch the members above them, so dropping them here retires
     *  those handlers before the state they read goes away. */
    Subscription PlayerChanges;
    /** This panel's one subscription to @ref AllClicks. */
    Subscription ClickListener;
};

}  // namespace VoltMod
