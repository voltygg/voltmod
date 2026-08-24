#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <string>
#include <utility>

namespace VoltMod::Menu
{

/** Read-only label row. Useful for headings or section dividers inside a menu. */
class TextOption : public MenuOption
{
public:
    explicit TextOption(std::string label) : _label(std::move(label)) {}
    std::string GetLabel(int /*slot*/) const override { return _label; }
    bool IsSelectable() const override { return false; }

private:
    std::string _label;
};

}  // namespace VoltMod::Menu
