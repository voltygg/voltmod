#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <VoltMod/Ui/UiWriteCache.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A live per-player binding to one Panorama layout: spawns it, keeps it, writes deltas.
 *
 * What a feature redrawing a layout every tick wants over the raw @ref UiPanel: it owns the
 * panel, re-spawns it when the entity is gone or too small for the slot being written, and drops
 * a write whose value the player already has - which is what makes a per-tick redraw affordable.
 * Every write is per player, so one entity shows each player their own content.
 *
 * Inert unless @ref Capability::CustomUi is on: @ref EnsureFor then fails, which is a caller's
 * cue to fall back rather than draw nothing.
 */
class UiLayout
{
public:
    /** @p layout is what @ref CustomUi::Spawn takes. Both @p ui and @p slots must outlive this
     *  object. Nothing is spawned until the first @ref EnsureFor, so constructing one is free. */
    UiLayout(CustomUi& ui, SlotEvents& slots, std::string layout);
    ~UiLayout();

    UiLayout(const UiLayout&) = delete;
    UiLayout& operator=(const UiLayout&) = delete;

    /** The layout resource this drives. */
    [[nodiscard]] const std::string& Name() const noexcept { return _layout; }

    /**
     * Make the entity exist and cover @p slot, spawning or re-spawning as needed. Call it before
     * a burst of writes for one player; the writes themselves do not check. False means the slot
     * cannot be written to, and the reason is logged once per spawn attempt, not once per frame.
     */
    bool EnsureFor(int slot);

    /** True when the entity exists and has per-player state for @p slot, so a write for that
     *  slot can land. Never spawns: the question a hide asks before touching an idle server. */
    [[nodiscard]] bool Covers(int slot) const;

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for @p slot only. */
    void Text(int slot, std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId, for @p slot only. */
    void Class(int slot, std::string_view panelId, std::string_view className, bool on);

    /** Give @p slot a cursor, without which nothing in the layout is clickable. */
    void InputCapture(int slot, bool enabled);

    /** Forget what @p slot was last written, so the next write goes through whatever its value. */
    void Forget(int slot);

    /** Call @p handler when @p buttonId is pressed in this layout, whichever entity is carrying
     *  it - unlike @ref UiPanel::OnClick this survives a re-spawn. Keep the Subscription. */
    [[nodiscard]] Subscription OnClick(std::string buttonId, std::function<void(int slot)> handler);

    /** Drop the entity and everything cached about it. The next @ref EnsureFor spawns a new one. */
    void Reset();

    /** Drive a different layout resource from here on, dropping the current entity. The object
     *  stays put, so a @ref UiList and every @ref OnClick subscription survive the swap. */
    void Retarget(std::string layout);

private:
    /** The entity handlers match against, shared with them so a re-spawn moves them all at once
     *  and this object's destructor silences them by clearing it. */
    using LiveRef = std::shared_ptr<EntityRef>;

    /** Spawn a fresh entity, dropping the old one and everything remembered about it. */
    bool Spawn();

    /** True when the write went through. A failure forgets the slot, so the next frame retries,
     *  and says why once rather than once per frame. */
    bool Wrote(int slot, const Status& status, std::string_view what);

    CustomUi& _ui;
    std::string _layout;
    UiPanel _panel;
    LiveRef _live = std::make_shared<EntityRef>();
    /** Set by the roster feed: the per-player state count is fixed when the entity spawns, so a
     *  slot that connected later is only reachable through a new one. Re-spawning on any other
     *  trigger would retry a hopeless spawn every frame. */
    bool _rosterChanged = true;
    UiWriteCache _cache;
    /** Declared last: its handler touches the members above it. */
    Subscription _roster;
};

}  // namespace VoltMod
