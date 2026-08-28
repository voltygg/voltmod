#include "Ui/LayoutName.hpp"
#include "Ui/UiClicks.hpp"
#include "Ui/UiPanelState.hpp"

#include <VoltMod/Ui/UiPanel.hpp>
#include <memory>
#include <string>
#include <utility>

namespace VoltMod
{

CustomUi::CustomUi(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
                   SlotEvents& slots, Scheduler& scheduler)
    : Clicked({.OnFirst = [this] { return _clicks->Install(); }, .OnLast = [this] { _clicks->Remove(); }}),
      _entities(entities),
      _ops(ops),
      _slots(slots),
      _clicks(std::make_unique<UiClicks>(interfaces, bindings, slots, entities, scheduler, Clicked))
{}

CustomUi::~CustomUi() = default;

Result<UiPanel> CustomUi::Panel(std::string_view layout)
{
    auto resource = ResolveLayoutName(layout);
    if (!resource)
        return std::unexpected(resource.error());

    return UiPanel(std::make_shared<UiPanelState>(&_entities, &_ops, &_slots, &Clicked, std::string(layout),
                                                  std::move(*resource)));
}

Result<UiPanel> CustomUi::Spawn(std::string_view layout)
{
    auto panel = Panel(layout);
    if (!panel)
        return panel;

    // Ensure only says whether it worked, and a caller asking for the entity now wants the reason
    // it did not, so the state's own spawn is what runs here.
    if (Status spawned = panel->State().Spawn(); !spawned)
        return std::unexpected(spawned.error());

    return panel;
}

}  // namespace VoltMod
