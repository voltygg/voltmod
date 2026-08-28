#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace VoltMod
{

/**
 * Boolean state row. Renders `"<title>: <onLabel|offLabel>"`. Both E and A/D flip the state.
 * State lives in the caller (engine field, EffectManager, etc.); pass getter/setter callbacks.
 */
class ToggleOption : public MenuOption
{
public:
    using GetFn = std::function<bool(int)>;
    using ToggleFn = std::function<void(int)>;

    ToggleOption(std::string title, std::string onLabel, std::string offLabel, GetFn get, ToggleFn toggle,
                 bool enabled = true)
        : _title(std::move(title)),
          _onLabel(std::move(onLabel)),
          _offLabel(std::move(offLabel)),
          _get(std::move(get)),
          _toggle(std::move(toggle))
    {
        _enabled = enabled;
    }

    MenuRow Describe(int slot) const override
    {
        bool on = _get && _get(slot);
        return {.Label = _title, .Value = on ? _onLabel : _offLabel, .Kind = MenuRowKind::Toggle};
    }

    void OnActivate(int slot, MenuHost& /*menus*/) override
    {
        if (_enabled && _toggle)
            _toggle(slot);
    }

    bool OnHorizontal(int slot, int /*direction*/) override
    {
        if (!_enabled || !_toggle)
            return false;
        _toggle(slot);
        return true;
    }

    bool UsesHorizontal() const override { return true; }

private:
    std::string _title;
    std::string _onLabel;
    std::string _offLabel;
    GetFn _get;
    ToggleFn _toggle;
};

}  // namespace VoltMod
