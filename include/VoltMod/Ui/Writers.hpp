#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

namespace VoltMod
{

/** One dialog variable on the layout root, what a `text="{s:var}"` Label reads. */
struct TextVar
{
    std::string_view Root;
    std::string_view Var;

    template <class Panel>
    void Write(Panel& panel, int slot, std::string_view value) const
    {
        panel.SetText(slot, Root, Var, value);
    }
};

/** One class on one panel, on or off. */
struct ClassFlag
{
    std::string_view Id;
    std::string_view Class;

    template <class Panel>
    void Write(Panel& panel, int slot, bool on) const
    {
        panel.SetClass(slot, Id, Class, on);
    }
};

/**
 * @brief One panel, a family of classes, exactly one of them on: an index into @ref Classes,
 * or @ref None.
 *
 * A `Prefix--variant` family - an icon set, a bar's steps, an accent colour. @ref Classes points
 * at the family's generated `inline constexpr` array rather than copying it, so a call site never
 * spells the variant count. Writes all of them so a stale one clears; the panel's write cache
 * makes the ones that did not change free. N panels sharing one class (tab selection) are an
 * array of @ref ClassFlag instead.
 *
 * A failed write is the panel's to report, not the caller's: it logs once and keeps going.
 */
struct ClassChoice
{
    /** The index that turns every class off. */
    static constexpr int None = -1;

    std::string_view Id;
    std::span<const std::string_view> Classes;

    [[nodiscard]] constexpr int Count() const { return static_cast<int>(Classes.size()); }

    /** The index of @p name: a class as written, or the variant after its `--`. @ref None for neither. */
    [[nodiscard]] constexpr int Find(std::string_view name) const
    {
        for (int i = 0; i < Count(); ++i)
        {
            const std::string_view cls = Classes[i];
            const auto dashes = cls.find("--");
            if (cls == name || (dashes != std::string_view::npos && cls.substr(dashes + 2) == name))
                return i;
        }
        return None;
    }

    template <class Panel>
    void Write(Panel& panel, int slot, int index) const
    {
        for (int i = 0; i < Count(); ++i)
            panel.SetClass(slot, Id, Classes[i], i == index);
    }
};

/**
 * @brief A panel and a slot named once, so a redraw reads as writer-value lines.
 *
 * Any writer with `Write(panel, slot, value)` fits. Templated on the panel type so tests drive a
 * fake; a plugin spells @ref UiPanelWriter.
 */
template <class Panel>
class PanelWriter
{
public:
    constexpr PanelWriter(Panel& panel, int slot) : _panel(panel), _slot(slot) {}

    template <class Writer, class Value>
    void Set(const Writer& writer, const Value& value) const
    {
        writer.Write(_panel, _slot, value);
    }

    [[nodiscard]] constexpr int Slot() const { return _slot; }

private:
    Panel& _panel;
    int _slot;
};

/**
 * One writer bundle per entry of a generated array: `MakeWriters(Hud::Cards, MakeCard)` turns the
 * `Cards` array a screen header emits into the plugin's own `std::array<CardWriters, N>`.
 */
template <class Entry, std::size_t N, class Make>
constexpr auto MakeWriters(const std::array<Entry, N>& entries, Make make)
{
    std::array<std::invoke_result_t<Make&, const Entry&>, N> made{};
    for (std::size_t i = 0; i < N; ++i)
        made[i] = make(entries[i]);
    return made;
}

}  // namespace VoltMod
