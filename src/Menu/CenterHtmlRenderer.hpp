#pragma once

#include "Menu/CenterHtmlRender.hpp"
#include "Menu/MenuCore.hpp"
#include "Menu/MenuRenderer.hpp"

namespace VoltMod
{

/**
 * @brief Menus drawn as center HTML, re-sent every frame.
 *
 * Needs nothing on the client, so every player can be drawn to; the keyboard is its only input.
 */
class CenterHtmlRenderer final : public MenuRenderer
{
public:
    /** @p services and @p core must outlive the renderer; @ref MenuManager owns both. */
    CenterHtmlRenderer(const MenuServices& services, MenuCore& core);

    bool Attach(int slot) override;
    bool Present(int slot) override;
    void Dismiss(int slot) override;
    [[nodiscard]] int RowsPerPage() const override { return ItemsPerPage; }

private:
    MenuServices _services;
    MenuCore& _core;
};

}  // namespace VoltMod
