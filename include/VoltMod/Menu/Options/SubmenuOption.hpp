#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace VoltMod
{

/**
 * Push a built submenu onto the player's menu stack. The factory is invoked lazily on E.
 *
 * The activation path lives in `MenuOption.cpp` because it calls into the manager, and
 * including `MenuManager.hpp` here would close a cycle through the manager → menu chain.
 */
class SubmenuOption : public MenuOption
{
public:
    using FactoryFn = std::function<std::shared_ptr<MenuView>(int)>;

    SubmenuOption(std::string label, FactoryFn factory, bool enabled = true)
        : _label(std::move(label)), _factory(std::move(factory))
    {
        _enabled = enabled;
    }

    std::string GetLabel(int /*slot*/) const override { return _label; }
    void OnActivate(int slot, MenuManager& menus) override;

private:
    std::string _label;
    FactoryFn _factory;
};

}  // namespace VoltMod
