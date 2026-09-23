#include "Ui/ButtonPressHook.hpp"
#include "Ui/LayoutPath.hpp"
#include "Ui/ScreenEntity.hpp"

#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace VoltMod
{

ScreenManager::ScreenManager(EntitySystem& entities, const Bindings& bindings, Interfaces& interfaces,
                             SlotEvents& slots, Scheduler& scheduler, Visibility& visibility)
    : Pressed({.OnFirst = [this] { return _hook->Install(); }, .OnLast = [this] { _hook->Remove(); }}),
      _entities(entities),
      _bindings(bindings),
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
    {
        return std::unexpected(Error::Invalid(std::format("slot {} is not a player slot", slot)));
    }

    if (auto visible = _visibility.Available(); !visible)
    {
        return std::unexpected(
            Error::Unsupported(std::format("a player screen needs the Visibility filter: {}", visible.error().Detail)));
    }

    return Create(layout, slot);
}

Status ScreenManager::Available() const
{
    const std::pair<bool, std::string_view> needed[] = {
        {static_cast<bool>(_bindings.CustomHudSetHasClass), "CCSCustomHudLayout::SetHasClass"},
        {static_cast<bool>(_bindings.CustomHudSetHasClassForPlayer), "CCSCustomHudLayout::SetHasClassForPlayer"},
        {static_cast<bool>(_bindings.CustomHudSetDialogVariable), "CCSCustomHudLayout::SetDialogVariableString"},
        {static_cast<bool>(_bindings.CustomHudSetDialogVariableForPlayer),
         "CCSCustomHudLayout::SetDialogVariableStringForPlayer"},
        {static_cast<bool>(_bindings.CustomHudSetInputCapture), "CCSCustomHudLayout::SetInputCaptureEnabled"},
        {static_cast<bool>(_bindings.FilterMessage), "INetworkMessageProcessingPreFilter::FilterMessage"},
        {static_cast<bool>(_bindings.ClientMessageFilter), "CServerSideClient::INetworkMessageProcessingPreFilter"},
        {static_cast<bool>(_bindings.ClientSlot), "CServerSideClientBase::m_nClientSlot"},
    };
    for (const auto& [bound, key] : needed)
    {
        if (!bound)
        {
            return std::unexpected(Error::Unsupported(std::format("gamedata '{}' did not bind", key)));
        }
    }
    return _visibility.Available();
}

Result<Screen> ScreenManager::Create(std::string_view layout, int owner)
{
    auto path = LayoutPath::Parse(layout);
    if (!path)
    {
        return std::unexpected(path.error());
    }

    return Screen(std::make_unique<ScreenEntity>(_entities, _slots, _visibility, std::move(*path), owner));
}

}  // namespace VoltMod
