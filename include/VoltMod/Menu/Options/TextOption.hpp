#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <string>
#include <utility>

namespace VoltMod
{

/** Read-only label row. Useful for headings or section dividers inside a menu. */
class TextOption : public MenuOption
{
public:
    explicit TextOption(std::string label) : _label(std::move(label)) {}
    MenuRow Describe(int /*slot*/) const override { return {.Label = _label, .Kind = MenuRowKind::Text}; }
    bool IsSelectable() const override { return false; }

private:
    std::string _label;
};

}  // namespace VoltMod
