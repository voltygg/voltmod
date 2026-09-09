#include "Menu/MenuLayout.hpp"

#include <charconv>
#include <cstddef>
#include <format>

namespace VoltMod
{

constexpr std::string_view RowActivate = "btn";
constexpr std::string_view RowDecrement = "dec";
constexpr std::string_view RowIncrement = "inc";

/** Panel ids carry the screen name, the dialog variables the generator writes do not. */
static std::string_view VarPrefix(const MenuLayoutSpec& spec, std::string_view idPrefix)
{
    if (idPrefix.size() > spec.Root.size() + 1 && idPrefix.starts_with(spec.Root) && idPrefix[spec.Root.size()] == '_')
        return idPrefix.substr(spec.Root.size() + 1);
    return idPrefix;
}

/** Accept only unsigned decimal indexes. Reject signs, leading zeros, and trailing text. */
static bool ParseIndex(std::string_view text, int& index)
{
    if (text.empty() || text.front() < '0' || text.front() > '9')
        return false;
    if (text.size() > 1 && text.front() == '0')
        return false;

    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, index);
    return error == std::errc{} && stop == end;
}

MenuRowIds RowIds(const MenuLayoutSpec& spec, int row)
{
    const std::string id = std::format("{}{}", spec.RowPrefix, row);
    const std::string var = std::format("{}{}", VarPrefix(spec, spec.RowPrefix), row);
    return {.Row = id,
            .Button = id + "_btn",
            .Dec = id + "_dec",
            .Inc = id + "_inc",
            .LabelVar = var + "_label",
            .ValueVar = var + "_value"};
}

std::string NavId(const MenuLayoutSpec& spec, int index)
{
    return std::format("{}{}", spec.NavPrefix, index);
}

std::string NavVar(const MenuLayoutSpec& spec, int index)
{
    return std::format("{}{}", VarPrefix(spec, spec.NavPrefix), index);
}

MenuPress ParseMenuButton(const MenuLayoutSpec& spec, std::string_view id)
{
    if (id.empty())
        return {};
    if (id == spec.BackId)
        return {.Button = MenuButton::Back};
    if (id == spec.CloseId)
        return {.Button = MenuButton::Close};
    if (id == spec.PrevId)
        return {.Button = MenuButton::Prev};
    if (id == spec.NextId)
        return {.Button = MenuButton::Next};
    if (id == spec.CancelId)
        return {.Button = MenuButton::Cancel};

    if (spec.NavCount > 0 && !spec.NavPrefix.empty() && id.starts_with(spec.NavPrefix))
    {
        int tab = -1;
        if (!ParseIndex(id.substr(spec.NavPrefix.size()), tab))
            return {};
        return {.Button = MenuButton::Nav, .Row = tab};
    }

    if (spec.RowPrefix.empty() || !id.starts_with(spec.RowPrefix))
        return {};

    const std::string_view rest = id.substr(spec.RowPrefix.size());
    const std::size_t split = rest.rfind('_');
    if (split == std::string_view::npos)
        return {};

    const std::string_view suffix = rest.substr(split + 1);
    MenuButton button = MenuButton::None;
    if (suffix == RowActivate)
        button = MenuButton::Row;
    else if (suffix == RowDecrement)
        button = MenuButton::RowDec;
    else if (suffix == RowIncrement)
        button = MenuButton::RowInc;
    else
        return {};

    int row = -1;
    if (!ParseIndex(rest.substr(0, split), row))
        return {};

    return {.Button = button, .Row = row};
}

std::string_view MenuKindClass(MenuRowKind kind)
{
    switch (kind)
    {
    case MenuRowKind::Text:
        return "Kind--text";
    case MenuRowKind::Submenu:
        return "Kind--submenu";
    case MenuRowKind::Toggle:
        return "Kind--toggle";
    case MenuRowKind::Choice:
        return "Kind--choice";
    case MenuRowKind::Input:
        return "Kind--input";
    case MenuRowKind::Button:
        break;
    }
    return "Kind--button";
}

}  // namespace VoltMod
