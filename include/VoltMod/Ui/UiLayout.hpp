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
 * @ref UiPanel is the raw handle - every call reaches the engine. This is what a feature driving
 * a layout every tick wants instead: it owns the panel, re-spawns it when the entity is gone or
 * too small for the slot being written, and drops a write whose value the player already has.
 * That last part is what makes a per-frame redraw affordable, because unlike center HTML a
 * networked layout does not need re-sending to stay on screen.
 *
 * Every write is per player (@ref UiPanel::For), so one entity shows each player their own
 * content - and a layout whose markup hides itself by default stays hidden for everyone the
 * feature has not written to.
 *
 * Inert unless @ref Capability::CustomUi is on: @ref EnsureFor then fails and the writes are
 * no-ops, which is a caller's cue to fall back rather than draw nothing.
 */
class UiLayout
{
public:
    /**
     * @param layout what @ref CustomUi::Spawn takes: a bare name, or a full resource name under
     *               `panorama/layout/custom_game/` with its source `.xml` extension.
     * Both @p ui and @p slots must outlive this object. Nothing is spawned until the first
     * @ref EnsureFor, so constructing one during load costs nothing and cannot fail.
     */
    UiLayout(CustomUi& ui, SlotEvents& slots, std::string layout);
    ~UiLayout();

    UiLayout(const UiLayout&) = delete;
    UiLayout& operator=(const UiLayout&) = delete;

    /** The layout resource this drives. */
    [[nodiscard]] const std::string& Name() const noexcept { return _layout; }

    /**
     * Make the entity exist and cover @p slot, spawning or re-spawning as needed.
     *
     * Call this before a burst of writes for one player; the writes themselves do not check.
     * False means this player cannot be written to - the capability is off, the engine refused
     * the entity, or the build carries no per-player layout state - and the reason is logged
     * once per spawn attempt rather than once per frame.
     */
    bool EnsureFor(int slot);

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for @p slot only. */
    void Text(int slot, std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId, for @p slot only. */
    void Class(int slot, std::string_view panelId, std::string_view className, bool on);

    /** Give @p slot a cursor, without which nothing in the layout is clickable. */
    void InputCapture(int slot, bool enabled);

    /** Forget what @p slot was last written, so the next write goes through whatever its value. */
    void Forget(int slot);

    /**
     * Call @p handler when @p buttonId is pressed in this layout, whichever entity is carrying it.
     *
     * Unlike @ref UiPanel::OnClick this survives a re-spawn, which is the point: the subscription
     * outlives any one entity. Keep the Subscription; presses stop when it is dropped, and every
     * handler goes quiet when this object is destroyed.
     */
    [[nodiscard]] Subscription OnClick(std::string buttonId, std::function<void(int slot)> handler);

    /** Drop the entity and everything cached about it. The next @ref EnsureFor spawns a new one. */
    void Reset();

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
