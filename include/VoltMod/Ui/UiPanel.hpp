#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One `custom_hud_layout`, owned: the entity, what each player has been told about it, and
 * the presses coming back from it.
 *
 * The destructor removes the entity, so a panel cannot outlive the plugin that spawned it across a
 * `meta reload`. Move-only, and every call re-resolves the entity, so a map change leaves the
 * panel empty and its destructor a no-op.
 *
 * A write is either global (@ref Everyone) or for one slot. A per-slot write goes through a cache:
 * the value a player already has is not sent again, which is what makes redrawing a layout every
 * frame affordable - unlike center HTML, a networked layout stays on screen without re-sending.
 *
 * A panel is shared or private. A shared panel is one entity every client receives, and a per-slot
 * write is that player's HUD, which a client shows for the pawn it is *viewing*. A private panel
 * (@ref UiPanels::Panel with a slot) is an entity only that viewer receives, written through the
 * global state, so it stays on screen while they are dead or spectating. Its writes name the
 * viewer or @ref Everyone; any other slot is refused.
 *
 * A write never spawns. @ref Prepare is the one spawn point, so a burst of writes for one player
 * costs one check rather than one per write, and a slot the entity does not cover fails with a
 * reason instead of churning entities.
 *
 * Does nothing unless @ref Capability::CustomUi is on: @ref Prepare then fails, which is a caller's cue
 * to fall back rather than draw nothing.
 */
class UiPanel
{
public:
    /** @ref EveryoneSlot: the layout's global state, not one player's. */
    static constexpr int Everyone = EveryoneSlot;

    /** An empty panel: owns nothing, and fails every write with @ref ErrorCode::NotFound. */
    UiPanel() = default;

    /** Removes the entity if it still resolves. */
    ~UiPanel();

    UiPanel(UiPanel&&) noexcept = default;
    /** Removes what this panel held before taking @p other's entity and click routing. */
    UiPanel& operator=(UiPanel&& other) noexcept;
    UiPanel(const UiPanel&) = delete;
    UiPanel& operator=(const UiPanel&) = delete;

    /** Whether the entity exists right now. False before the first @ref Prepare, and after a map
     *  change or an explicit @ref Remove. */
    explicit operator bool() const;

    /** The layout resource this panel drives, as it was named. */
    [[nodiscard]] std::string_view LayoutName() const noexcept;

    /** The entity behind this panel, for logging or comparing against a @ref UiClick. */
    [[nodiscard]] EntityRef Entity() const noexcept;

    /** The one slot a private panel is networked to, or @ref Everyone for a shared panel. */
    [[nodiscard]] int Viewer() const noexcept;

    /** How many per-player states the entity carries, or -1 when there is no entity. Zero leaves
     *  only @ref Everyone writes. */
    [[nodiscard]] int PlayerStateCount() const;

    /**
     * Make the entity exist and cover @p slot, spawning or re-spawning as needed. Call it before a
     * burst of writes for one player, or with @ref Everyone to spawn for global writes only; the
     * writes themselves do not spawn. False means the slot cannot be written to, and the reason is
     * logged once per spawn attempt rather than once per frame.
     */
    bool Prepare(int slot);

    /** True when the entity exists and has per-player state for @p slot, so a write for that slot
     *  can land. Never spawns: the question a hide asks before touching an idle server. */
    [[nodiscard]] bool CanWrite(int slot) const;

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for @p slot or
     *  @ref Everyone. Fails rather than spawning when the entity does not cover @p slot. */
    Status SetText(int slot, std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId, for @p slot or @ref Everyone. */
    Status SetClass(int slot, std::string_view panelId, std::string_view className, bool on);

    /** Hand @p className back to whatever the layout markup itself says. Not cached: it is a
     *  correction, so it goes out whenever it is asked for. */
    Status RestoreClass(int slot, std::string_view panelId, std::string_view className);

    /** Give @p slot (or @ref Everyone) a cursor. Nothing in a layout is clickable without this:
     *  the game keeps mouse-look and the panel never sees a pointer. */
    Status SetInputCapture(int slot, bool enabled);

    /** Forget what @p slot was last told, so the next write goes through whatever its value. */
    void ForgetWrites(int slot);

    /** Remove the entity now instead of at destruction, and forget what every player was told.
     *  Safe to call again; the next @ref Prepare spawns a fresh one. */
    void Remove();

    /** Drive a different layout resource from here on, dropping the current entity. The panel
     *  stays put, so every @ref Clicked and @ref Pressed subscription survives the swap. */
    void SetLayout(std::string layout);

    /**
     * Every press in **this** layout, whichever Button it was, and whichever entity is carrying
     * the layout - so a subscription survives a re-spawn and a move of the panel.
     *
     * Subscribing is what installs the click hook (@ref UiPanels::Clicked), and dropping the last
     * subscription across this event and every @ref Pressed removes it again.
     */
    Event<const UiClick&>& Clicked();

    /**
     * Presses of one Button id in this layout:
     *
     * @code
     * _accept = _panel.Pressed("accept") += [this](int slot) { Accept(slot); };
     * @endcode
     *
     * Filtered by both the layout and the id, so two layouts sharing a button id do not trigger
     * each other. The event is created on first use and outlives every re-spawn; @p id is compared
     * against client-controlled text, so name ids you authored rather than parsing them.
     */
    Event<int>& Pressed(std::string_view id);

private:
    friend class UiPanels;

    explicit UiPanel(std::shared_ptr<UiPanelState> state) noexcept : _state(std::move(state)) {}

    /** The state, created on demand: @ref Clicked and @ref Pressed have to hand out a live event
     *  even from a panel that has no entity system behind it. */
    UiPanelState& State();

    /** The panel's state, on the heap so click routing keeps working after a move: the one
     *  subscription to @ref UiPanels::Clicked points at this, never at the panel. Null for an
     *  empty or moved-from panel, which is what every call above checks for. */
    std::shared_ptr<UiPanelState> _state;
};

}  // namespace VoltMod
