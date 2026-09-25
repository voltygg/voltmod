#include "Ui/ScreenEntity.hpp"

#include <VoltMod/Ui/Screen.hpp>
#include <string>
#include <utility>

namespace VoltMod
{

Screen::Screen() = default;

Screen::Screen(std::unique_ptr<ScreenEntity> entity) : _entity(std::move(entity)) {}

Screen::~Screen() = default;

Screen::Screen(Screen&&) noexcept = default;

Screen& Screen::operator=(Screen&&) noexcept = default;

Screen::operator bool() const
{
    return _entity && _entity->Exists();
}

bool Screen::EnsureSpawned(int slot)
{
    return _entity && _entity->EnsureSpawned(slot);
}

void Screen::SetText(int slot, std::string_view variable, std::string_view value)
{
    if (_entity)
    {
        _entity->WriteText(slot, variable, value);
    }
}

void Screen::SetClass(int slot, std::string_view elementId, std::string_view className, bool on)
{
    if (_entity)
    {
        _entity->WriteClass(slot, elementId, className, on);
    }
}

void Screen::SetHidden(int slot, std::string_view elementId, bool hidden)
{
    SetClass(slot, elementId, "hidden", hidden);
}

void Screen::SetModifier(int slot, std::string_view elementId, std::string_view block,
                         std::span<const std::string_view> names, std::string_view name)
{
    std::string className;
    for (std::string_view each : names)
    {
        className.assign(block).append("--").append(each);
        SetClass(slot, elementId, className, each == name);
    }
}

void Screen::ShowCursor(int slot, bool shown)
{
    if (_entity)
    {
        _entity->WriteCursor(slot, shown);
    }
}

void Screen::Remove()
{
    if (_entity)
    {
        _entity->Remove();
    }
}

}  // namespace VoltMod
