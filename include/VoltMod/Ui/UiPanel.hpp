#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
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
 * panel falsy and its destructor a no-op.
 *
 * A write is either global (@ref Everyone) or for one slot. A per-slot write goes through a cache:
 * the value a player already has is not sent again, which is what makes redrawing a layout every
 * frame affordable - unlike center HTML, a networked layout stays on screen without re-sending.
 *
 * A write never spawns. @ref Ensure is the one spawn point, so a burst of writes for one player
 * costs one check rather than one per write, and a slot the entity does not cover fails with a
 * reason instead of churning entities.
 *
 * Inert unless @ref Capability::CustomUi is on: @ref Ensure then fails, which is a caller's cue to
 * fall back rather than draw nothing.
 */
class UiPanel
{
public:
    /** Slot value meaning "the layout's global state", not one player's. */
    static constexpr int Everyone = -1;

    /** An empty panel: falsy, owns nothing, and fails every write with @ref ErrorCode::NotFound. */
    UiPanel() = default;

    /** Removes the entity if it still resolves. */
    ~UiPanel();

    UiPanel(UiPanel&&) noexcept = default;
    /** Removes what this panel held before taking @p other's entity and click routing. */
    UiPanel& operator=(UiPanel&& other) noexcept;
    UiPanel(const UiPanel&) = delete;
    UiPanel& operator=(const UiPanel&) = delete;

    /** Whether the entity exists right now. False before the first @ref Ensure, and after a map
     *  change or an explicit @ref Remove. */
    explicit operator bool() const;

    /** The layout resource this panel drives, as it was named. */
    [[nodiscard]] std::string_view Name() const noexcept;

    /** The entity behind this panel, for logging or comparing against a @ref UiClick. */
    [[nodiscard]] EntityRef Ref() const noexcept;

    /** How many per-player states the entity carries, or -1 when there is no entity. Zero leaves
     *  only @ref Everyone writes. */
    [[nodiscard]] int PlayerStateCount() const;

    /**
     * Make the entity exist and cover @p slot, spawning or re-spawning as needed. Call it before a
     * burst of writes for one player, or with @ref Everyone to spawn for global writes only; the
     * writes themselves do not spawn. False means the slot cannot be written to, and the reason is
     * logged once per spawn attempt rather than once per frame.
     */
    bool Ensure(int slot);

    /** True when the entity exists and has per-player state for @p slot, so a write for that slot
     *  can land. Never spawns: the question a hide asks before touching an idle server. */
    [[nodiscard]] bool Covers(int slot) const;

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for @p slot or
     *  @ref Everyone. Fails rather than spawning when the entity does not cover @p slot. */
    Status Text(int slot, std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId, for @p slot or @ref Everyone. */
    Status Class(int slot, std::string_view panelId, std::string_view className, bool on);

    /** Hand @p className back to whatever the layout markup itself says. Not cached: it is a
     *  correction, so it goes out whenever it is asked for. */
    Status ResetClass(int slot, std::string_view panelId, std::string_view className);

    /** Give @p slot (or @ref Everyone) a cursor. Nothing in a layout is clickable without this:
     *  the game keeps mouse-look and the panel never sees a pointer. */
    Status InputCapture(int slot, bool enabled);

    /** Forget what @p slot was last told, so the next write goes through whatever its value. */
    void Forget(int slot);

    /** Remove the entity now instead of at destruction, and forget what every player was told.
     *  Idempotent; the next @ref Ensure spawns a fresh one. */
    void Remove();

    /** Drive a different layout resource from here on, dropping the current entity. The panel
     *  stays put, so every @ref Clicked and @ref Button subscription survives the swap. */
    void SetLayout(std::string layout);

    /**
     * Every press in **this** layout, whichever Button it was, and whichever entity is carrying
     * the layout - so a subscription survives a re-spawn and a move of the panel.
     *
     * Subscribing is what installs the click hook (@ref CustomUi::Clicked), and dropping the last
     * subscription across this event and every @ref Button removes it again.
     */
    Event<const UiClick&>& Clicked();

    /**
     * Presses of one Button id in this layout:
     *
     * @code
     * _accept = _panel.Button("accept") += [this](int slot) { Accept(slot); };
     * @endcode
     *
     * Filtered by both the layout and the id, so two layouts sharing a button id do not trigger
     * each other. The event is created on first use and outlives every re-spawn; @p id is compared
     * against client-controlled text, so name ids you authored rather than parsing them.
     */
    Event<int>& Button(std::string_view id);

private:
    friend class CustomUi;

    explicit UiPanel(std::shared_ptr<UiPanelState> state) noexcept : _state(std::move(state)) {}

    /** The state, created on demand: @ref Clicked and @ref Button have to hand out a live event
     *  even from a panel that has no entity system behind it. */
    UiPanelState& State();

    /** The panel's state, on the heap so click routing keeps working after a move: the one
     *  subscription to @ref CustomUi::Clicked points at this, never at the panel. Null for an
     *  empty or moved-from panel, which is what every call above checks for. */
    std::shared_ptr<UiPanelState> _state;
};

/**
 * @brief Spawns `custom_hud_layout` panels and owns the hook their Buttons report through.
 *
 * Layouts are independent entities, so one plugin's HUD does not disturb another's.
 */
class CustomUi
{
public:
    /** All must outlive this service; the Runtime declares them above it. */
    CustomUi(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
             SlotEvents& slots, Scheduler& scheduler);
    ~CustomUi();

    CustomUi(const CustomUi&) = delete;
    CustomUi& operator=(const CustomUi&) = delete;

    /**
     * A panel for @p layout, spawned on its first @ref UiPanel::Ensure.
     *
     * @p layout is a bare name (`"welcome"`), or a full resource name under
     * `panorama/layout/custom_game/` with its **source** `.xml` extension - the one directory the
     * addon whitelist allows, and the client rejects anything else silently. Refusing the name
     * here is the point: a bad one renders nothing and says so only on the client console.
     */
    Result<UiPanel> Panel(std::string_view layout);

    /** @ref Panel plus the spawn, for a panel driven by global writes: the same errors, plus the
     *  engine's reason for refusing the entity. */
    Result<UiPanel> Spawn(std::string_view layout);

    /**
     * Presses from **every** layout, including one another plugin spawned - diagnostics, and the
     * unfiltered form behind @ref UiPanel::Clicked, which is what a plugin driving its own panel
     * wants.
     *
     * Dormant until something subscribes and removed when the last subscription drops, the panels
     * routing through it included. The hooked vfunc sits in a secondary vtable that can only be
     * located from a connected client, so subscribing on an empty server arms on the next connect.
     * Inert when @ref Capability::UiClicks is off, where subscribing is refused after saying why.
     */
    Event<const UiClick&> Clicked;

private:
    EntitySystem& _entities;
    EntityOps& _ops;
    SlotEvents& _slots;
    /** Declared after @ref Clicked so the hook is gone before the event it raises into. */
    std::unique_ptr<UiClicks> _clicks;
};

}  // namespace VoltMod
