#include "Menu/PanoramaIds.hpp"

#include <charconv>
#include <cstddef>
#include <format>

namespace VoltMod
{

std::string MenuRowId(int row)
{
    return std::format("{}{}", MenuIds::RowPrefix, row);
}

// The row index as the layout writes it: decimal digits and nothing else, so "01", "+1", "-1" and
// "1x" are all refused rather than rounded to a row a press could land on. from_chars would take
// the sign and stop short of trailing junk, so the first character is checked here and the whole
// view is required below.
static bool ParseRowIndex(std::string_view text, int& row)
{
    if (text.empty() || text.front() < '0' || text.front() > '9')
        return false;
    if (text.size() > 1 && text.front() == '0')
        return false;

    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, row);
    return error == std::errc{} && stop == end;
}

MenuPress ParseMenuButton(std::string_view id)
{
    if (id == MenuIds::Back)
        return {.Button = MenuButton::Back};
    if (id == MenuIds::Close)
        return {.Button = MenuButton::Close};
    if (id == MenuIds::Prev)
        return {.Button = MenuButton::Prev};
    if (id == MenuIds::Next)
        return {.Button = MenuButton::Next};
    if (id == MenuIds::Cancel)
        return {.Button = MenuButton::Cancel};

    if (!id.starts_with(MenuIds::RowPrefix))
        return {};

    std::string_view rest = id.substr(MenuIds::RowPrefix.size());
    const std::size_t split = rest.rfind('_');
    if (split == std::string_view::npos)
        return {};

    const std::string_view suffix = rest.substr(split + 1);
    MenuButton button = MenuButton::None;
    if (suffix == MenuIds::RowActivate)
        button = MenuButton::Row;
    else if (suffix == MenuIds::RowDecrement)
        button = MenuButton::RowDec;
    else if (suffix == MenuIds::RowIncrement)
        button = MenuButton::RowInc;
    else
        return {};

    int row = -1;
    if (!ParseRowIndex(rest.substr(0, split), row))
        return {};

    return {.Button = button, .Row = row};
}

}  // namespace VoltMod
