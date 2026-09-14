#include "Ui/ButtonPressHook.hpp"
#include "Ui/LayoutPath.hpp"
#include "Ui/ScreenEntity.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <format>
#include <memory>
#include <utility>

namespace VoltMod
{

ScreenManager::ScreenManager(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
                             SlotEvents& slots, Scheduler& scheduler, Visibility& visibility)
    : Pressed({.OnFirst = [this] { return _hook->Install(); }, .OnLast = [this] { _hook->Remove(); }}),
      _entities(entities),
      _ops(ops),
      _slots(slots),
      _visibility(visibility),
      _hook(std::make_unique<ButtonPressHook>(interfaces, bindings, scheduler, Pressed))
{}

ScreenManager::~ScreenManager() = default;

Result<Screen> ScreenManager::Shared(std::string_view layout)
{
    return Create(layout, EveryoneSlot);
}

Result<Screen> ScreenManager::ForPlayer(std::string_view layout, int slot)
{
    if (!IsValidSlot(slot))
        return std::unexpected(Error::Invalid(std::format("slot {} is not a player slot", slot)));

    if (!_visibility.IsActive())
        return std::unexpected(Error::Unsupported("a player screen needs the Visibility filter, which is off"));

    return Create(layout, slot);
}

Result<Screen> ScreenManager::Create(std::string_view layout, int owner)
{
    auto path = LayoutPath::Parse(layout);
    if (!path)
        return std::unexpected(path.error());

    return Screen(std::make_unique<ScreenEntity>(_entities, _ops, _slots, _visibility, std::move(*path), owner));
}

}  // namespace VoltMod
