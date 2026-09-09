#pragma once

#include <VoltMod/Core/Result.hpp>
#include <array>
#include <string_view>

namespace VoltMod
{

/** One dialog variable on the layout root, what a `text="{s:var}"` Label reads. */
struct Text
{
    std::string_view Root;
    std::string_view Var;

    template <class Panel>
    Status Write(Panel& panel, int slot, std::string_view value) const
    {
        return panel.Text(slot, Root, Var, value);
    }
};

/** One class on one panel, on or off. */
struct Flag
{
    std::string_view Id;
    std::string_view Class;

    template <class Panel>
    Status Write(Panel& panel, int slot, bool on) const
    {
        return panel.Class(slot, Id, Class, on);
    }
};

/**
 * @brief Exactly one of N on: index into @ref Ids (and @ref Classes), -1 for none.
 *
 * Writes all N so a stale one clears; the panel's cache makes unchanged writes free. Ids may all
 * be the same panel (icon set, bar steps, accent) or N panels sharing one class (tab selection).
 */
template <int N>
struct OneOf
{
    static_assert(N > 0);

    std::array<std::string_view, N> Ids;
    std::array<std::string_view, N> Classes;

    template <class Panel>
    Status Write(Panel& panel, int slot, int index) const
    {
        Status result;
        for (int i = 0; i < N; ++i)
        {
            Status status = panel.Class(slot, Ids[i], Classes[i], i == index);
            if (!status && result)
                result = status;  // first failure wins, remaining writes still happen
        }
        return result;
    }
};

}  // namespace VoltMod
