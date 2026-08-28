#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <functional>
#include <string>
#include <utility>

namespace VoltMod
{

/** Plain action row; E invokes the callback. */
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

    /** Action row whose label is evaluated when rendered. */
    ButtonOption(LabelFn dynamicLabel, ActivateFn onActivate, bool enabled = true)
        : _dynamicLabel(std::move(dynamicLabel)), _onActivate(std::move(onActivate))
    {
        _enabled = enabled;
    }

    std::string GetLabel(int /*slot*/) const override { return _dynamicLabel ? _dynamicLabel() : _label; }

    void OnActivate(int slot, HtmlMenuManager& /*menus*/) override
    {
        if (_enabled && _onActivate)
            _onActivate(slot);
    }

private:
    std::string _label;
    LabelFn _dynamicLabel;
    ActivateFn _onActivate;
};

}  // namespace VoltMod
