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

/** Numeric `[min, max]` value with `step`. A/D adjusts; E does nothing by default. */
class SliderOption : public MenuOption
{
public:
    using GetFn = std::function<int(int)>;
    using SetFn = std::function<void(int, int)>;

    SliderOption(std::string title, int min, int max, int step, GetFn get, SetFn set, bool enabled = true)
        : _title(std::move(title)), _min(min), _max(max), _step(step), _get(std::move(get)), _set(std::move(set))
    {
        _enabled = enabled;
    }

    std::string GetLabel(int slot) const override
    {
        int val = std::clamp(_get ? _get(slot) : _min, _min, _max);
        constexpr int Cells = 10;
        int filled = (_max == _min) ? 0 : ((val - _min) * Cells) / (_max - _min);
        return std::format("{}: [{}] {}/{}", _title, Detail::RenderBar(filled, Cells), val, _max);
    }

    bool OnHorizontal(int slot, int direction) override
    {
        if (!_enabled || !_get || !_set)
            return false;
        int val = std::clamp(_get(slot) + direction * _step, _min, _max);
        _set(slot, val);
        return true;
    }

    bool UsesHorizontal() const override { return true; }

private:
    std::string _title;
    int _min;
    int _max;
    int _step;
    GetFn _get;
    SetFn _set;
};

}  // namespace VoltMod::Menu
