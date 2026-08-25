#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace VoltMod::Menu
{

/**
 * Like @ref ChoiceOption but uses a formatter to derive the label from the value.
 * Use when values are not naturally string-labeled (e.g. seconds → `"5m"`).
 */
template <typename T>
class SelectorOption : public MenuOption
{
public:
    using FormatFn = std::function<std::string(const T&)>;
    using GetIndexFn = std::function<int(int)>;
    using SetIndexFn = std::function<void(int, int)>;
    using CommitFn = std::function<void(int, const T&)>;

    SelectorOption(std::string title, std::vector<T> values, FormatFn formatter, GetIndexFn getIndex,
                   SetIndexFn setIndex, CommitFn onCommit = nullptr, bool enabled = true)
        : _title(std::move(title)),
          _values(std::move(values)),
          _formatter(std::move(formatter)),
          _getIndex(std::move(getIndex)),
          _setIndex(std::move(setIndex)),
          _onCommit(std::move(onCommit))
    {
        _enabled = enabled;
    }

    std::string GetLabel(int slot) const override
    {
        if (_values.empty())
            return _title;
        int idx = ClampIndex(slot);
        std::string label = _formatter ? _formatter(_values[idx]) : std::string{};
        return std::format("{}: &lt; {} &gt;", _title, label);
    }

    void OnActivate(int slot, MenuManager& /*menus*/) override
    {
        if (!_enabled || _values.empty())
            return;

        if (_onCommit)
        {
            _onCommit(slot, _values[ClampIndex(slot)]);
            return;
        }

        // No explicit commit callback - E advances like D so the row stays interactive
        // for plain "pick a value" menus where there's no separate apply step.
        OnHorizontal(slot, +1);
    }

    bool OnHorizontal(int slot, int direction) override
    {
        if (!_enabled || _values.empty() || !_setIndex)
            return false;
        int n = static_cast<int>(_values.size());
        int idx = ClampIndex(slot);
        idx = ((idx + direction) % n + n) % n;
        _setIndex(slot, idx);
        return true;
    }

    bool UsesHorizontal() const override { return true; }

private:
    int ClampIndex(int slot) const
    {
        int n = static_cast<int>(_values.size());
        int idx = _getIndex ? _getIndex(slot) : 0;
        return std::clamp(idx, 0, n - 1);
    }

    std::string _title;
    std::vector<T> _values;
    FormatFn _formatter;
    GetIndexFn _getIndex;
    SetIndexFn _setIndex;
    CommitFn _onCommit;
};

}  // namespace VoltMod::Menu
