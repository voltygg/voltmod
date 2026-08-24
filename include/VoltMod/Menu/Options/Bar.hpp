#pragma once

#include <string>

namespace VoltMod::Menu::Detail
{

/** Render a fixed-width unicode bar with @p filled cells out of @p cells. */
inline std::string RenderBar(int filled, int cells)
{
    std::string bar;
    bar.reserve(cells * 3);
    for (int i = 0; i < cells; ++i)
        bar += (i < filled ? "▮" : "▯");
    return bar;
}

}  // namespace VoltMod::Menu::Detail
