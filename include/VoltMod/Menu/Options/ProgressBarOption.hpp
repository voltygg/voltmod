#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Menu/Options/Bar.hpp>
#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace VoltMod::Menu
{

/** Read-only progress bar. Non-selectable. */
class ProgressBarOption : public MenuOption
{
public:
    using GetFn = std::function<int(int)>;

    ProgressBarOption(std::string title, GetFn get, int max) : _title(std::move(title)), _get(std::move(get)), _max(max)
    {}

    std::string GetLabel(int slot) const override
    {
        int val = _get ? _get(slot) : 0;
        constexpr int Cells = 10;
        int filled = (_max <= 0) ? 0 : std::clamp((val * Cells) / _max, 0, Cells);
        return std::format("{}: [{}] {}/{}", _title, Detail::RenderBar(filled, Cells), val, _max);
    }

    bool IsSelectable() const override { return false; }

private:
    std::string _title;
    GetFn _get;
    int _max;
};

}  // namespace VoltMod::Menu
