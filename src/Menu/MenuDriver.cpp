#include "Menu/MenuDriver.hpp"

#include "Menu/MenuKeys.hpp"

namespace VoltMod
{

bool MenuDriver::HandleKeys(int slot)
{
    return _keys.Handle(slot, *this);
}

}  // namespace VoltMod
