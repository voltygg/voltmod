#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <functional>
#include <string>
#include <utility>

namespace VoltMod::Menu
{

/** Plain action row. E fires the callback. Replaces the legacy `AddItem`. */
class ButtonOption : public MenuOption
{
public:
    using LabelFn = std::function<std::string()>;
    using ActivateFn = std::function<void(int)>;

    ButtonOption(std::string label, ActivateFn onActivate, bool enabled = true)
        : _label(std::move(label)), _onActivate(std::move(onActivate))
    {
        _enabled = enabled;
    }

    /** Dynamic-label variant - replaces the legacy `AddDynamicItem`. */
    ButtonOption(LabelFn dynamicLabel, ActivateFn onActivate, bool enabled = true)
        : _dynamicLabel(std::move(dynamicLabel)), _onActivate(std::move(onActivate))
    {
        _enabled = enabled;
    }

    std::string GetLabel(int /*slot*/) const override { return _dynamicLabel ? _dynamicLabel() : _label; }

    void OnActivate(int slot) override
    {
        if (_enabled && _onActivate)
            _onActivate(slot);
    }

private:
    std::string _label;
    LabelFn _dynamicLabel;
    ActivateFn _onActivate;
};

}  // namespace VoltMod::Menu
