#pragma once

#include "Menu/CenterHtmlRender.hpp"
#include "Menu/MenuDriver.hpp"

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>

namespace VoltMod
{

/** Renders keyboard-driven center HTML menus. */
class CenterHtmlDriver final : public MenuDriver
{
public:
    CenterHtmlDriver(ActiveMenus& menus, MenuSession& session, const MenuServices& services);

    bool Present(int slot) override;
    void Dismiss(int slot) override;

    [[nodiscard]] int RowsPerPage() const override { return ItemsPerPage; }
};

}  // namespace VoltMod
