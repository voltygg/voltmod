#pragma once

#include <VoltMod/Core/Result.hpp>
#include <span>
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
 * @brief One panel, a family of classes, exactly one of them on: an index into @ref Classes,
 * -1 for none.
 *
 * A `Prefix--variant` family - an icon set, a bar's steps, an accent colour. @ref Classes points
 * at the family's generated `inline constexpr` array rather than copying it, so a call site never
 * spells the variant count. Writes all of them so a stale one clears; the panel's write cache
 * makes the ones that did not change free. N panels sharing one class (tab selection) are an
 * array of @ref Flag instead.
 */
struct Choice
{
    std::string_view Id;
    std::span<const std::string_view> Classes;

    [[nodiscard]] constexpr int Count() const { return static_cast<int>(Classes.size()); }

    /** The index of @p name: a class as written, or the variant after its `--`. -1 for neither. */
    [[nodiscard]] constexpr int Find(std::string_view name) const
    {
        for (int i = 0; i < Count(); ++i)
        {
            const std::string_view cls = Classes[i];
            const auto dashes = cls.find("--");
            if (cls == name || (dashes != std::string_view::npos && cls.substr(dashes + 2) == name))
                return i;
        }
        return -1;
    }

    template <class Panel>
    Status Write(Panel& panel, int slot, int index) const
    {
        Status result;
        for (int i = 0; i < Count(); ++i)
        {
            Status status = panel.Class(slot, Id, Classes[i], i == index);
            if (!status && result)
                result = status;  // first failure wins, remaining writes still happen
        }
        return result;
    }
};

}  // namespace VoltMod
