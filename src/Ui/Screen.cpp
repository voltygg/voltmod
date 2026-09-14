#include "Ui/ScreenEntity.hpp"

#include <VoltMod/Ui/Screen.hpp>
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

Status Screen::SetText(int slot, std::string_view variable, std::string_view value)
{
    return _entity ? _entity->WriteText(slot, variable, value) : std::unexpected(Empty());
}

Status Screen::SetClass(int slot, std::string_view elementId, std::string_view className, bool on)
{
    return _entity ? _entity->WriteClass(slot, elementId, className, on) : std::unexpected(Empty());
}

Status Screen::SetHidden(int slot, std::string_view elementId, bool hidden)
{
    return SetClass(slot, elementId, "Hidden", hidden);
}

Status Screen::ShowCursor(int slot, bool shown)
{
    return _entity ? _entity->WriteCursor(slot, shown) : std::unexpected(Empty());
}

void Screen::Remove()
{
    if (_entity)
        _entity->Remove();
}

Error Screen::Empty()
{
    return Error::NotFound("this screen was never created by ScreenManager");
}

}  // namespace VoltMod
