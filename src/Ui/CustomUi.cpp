#include "Ui/LayoutName.hpp"
#include "Ui/UiClicks.hpp"
#include "Ui/UiPanelState.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace VoltMod
{

CustomUi::CustomUi(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
                   SlotEvents& slots, Scheduler& scheduler, Visibility& visibility)
    : Clicked({.OnFirst = [this] { return _clicks->Install(); }, .OnLast = [this] { _clicks->Remove(); }}),
      _entities(entities),
      _ops(ops),
      _slots(slots),
      _visibility(visibility),
      _clicks(std::make_unique<UiClicks>(interfaces, bindings, slots, entities, scheduler, Clicked))
{}

CustomUi::~CustomUi() = default;

Result<UiPanel> CustomUi::Panel(std::string_view layout, int viewer)
{
    auto resource = ResolveLayoutName(layout);
    if (!resource)
        return std::unexpected(resource.error());

    if (viewer != UiPanel::Everyone)
    {
        if (!IsValidSlot(viewer))
            return std::unexpected(Error::Invalid(std::format("slot {} is not a player slot", viewer)));

        // Without the filter the entity reaches every client - the opposite of the promise.
        if (!_visibility.IsActive())
            return std::unexpected(Error::Unsupported("a private panel needs the Visibility filter, which is inert"));
    }

    return UiPanel(std::make_shared<UiPanelState>(&_entities, &_ops, &_slots, &Clicked, std::string(layout),
                                                  std::move(*resource), &_visibility, viewer));
}

Result<UiPanel> CustomUi::Spawn(std::string_view layout, int viewer)
{
    auto panel = Panel(layout, viewer);
    if (!panel)
        return panel;

    // Ensure only says whether it worked, and a caller asking for the entity now wants the reason
    // it did not, so the state's own spawn is what runs here.
    if (Status spawned = panel->State().Spawn(); !spawned)
        return std::unexpected(spawned.error());

    return panel;
}

}  // namespace VoltMod
