#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <memory>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Spawns `custom_hud_layout` panels and owns the hook their Buttons report through.
 *
 * Layouts are independent entities, so one plugin's HUD does not disturb another's.
 */
class UiPanels
{
public:
    /** All must outlive this service; the Runtime declares them above it. */
    UiPanels(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
             SlotEvents& slots, Scheduler& scheduler, Visibility& visibility);
    ~UiPanels();

    UiPanels(const UiPanels&) = delete;
    UiPanels& operator=(const UiPanels&) = delete;

    /**
     * A panel for @p layout, spawned on its first @ref UiPanel::Prepare.
     *
     * @p layout is a bare name (`"welcome"`), or a full resource name under
     * `panorama/layout/custom_game/` with its **source** `.xml` extension - the one directory the
     * addon whitelist allows, and the client rejects anything else silently. Refusing the name
     * here is the point: a bad one renders nothing and says so only on the client console.
     *
     * With a @p viewer the panel is private to that slot: networked to that client alone
     * (@ref Capability::Visibility; refused while the filter is off, since the entity would then
     * reach everyone) and removed when the slot changes hands.
     */
    Result<UiPanel> Panel(std::string_view layout, int viewer = UiPanel::Everyone);

    /** @ref Panel plus the spawn, for a panel driven by global writes: the same errors, plus the
     *  engine's reason for refusing the entity. */
    Result<UiPanel> Spawn(std::string_view layout, int viewer = UiPanel::Everyone);

    /**
     * Presses from **every** layout, including one another plugin spawned - diagnostics, and the
     * unfiltered form behind @ref UiPanel::Clicked, which is what a plugin driving its own panel
     * wants.
     *
     * Dormant until something subscribes and removed when the last subscription drops, the panels
     * routing through it included. The hooked vfunc sits in a secondary vtable that can only be
     * located from a connected client, so subscribing on an empty server hooks on the next connect.
     * Does nothing when @ref Capability::UiClicks is off: subscribing is refused after saying why.
     */
    Event<const UiClick&> Clicked;

private:
    EntitySystem& _entities;
    EntityOps& _ops;
    SlotEvents& _slots;
    Visibility& _visibility;
    /** Declared after @ref Clicked so the hook is gone before the event it raises into. */
    std::unique_ptr<UiClickHook> _clicks;
};

}  // namespace VoltMod
