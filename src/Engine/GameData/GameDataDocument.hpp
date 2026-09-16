#pragma once

#include <VoltMod/Core/Json.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// In-memory representation of gamedata.jsonc. Strict reflection rejects unknown keys, and the
// platform columns avoid the `linux` macro defined by GCC.

namespace VoltMod
{

struct GameDataDocument
{
    struct Build
    {
        std::string server;    ///< steam.inf ServerVersion the entries were verified on.
        std::string verified;  ///< YYYY-MM-DD of the last full review.
    };

    struct Function
    {
        std::string Module = "server";
        std::optional<std::string> Windows;
        std::optional<std::string> Linux;
    };

    /** A pattern and the offset of its rel32 displacement. */
    struct GlobalColumn
    {
        std::string pattern;
        int rel32At = 0;
    };

    struct Global
    {
        std::string Module = "server";
        std::optional<GlobalColumn> Windows;
        std::optional<GlobalColumn> Linux;
    };

    /** A vtable slot, counted in `class` or its named `base`. */
    struct VTable
    {
        std::string Class;
        std::string Base;
        std::string Module = "server";
        std::optional<int> Windows;
        std::optional<int> Linux;
    };

    /** A platform offset, or the RTTI-derived location of `base` within `class`. */
    struct Offset
    {
        std::optional<int> Windows;
        std::optional<int> Linux;
        std::string Class;
        std::string Base;
        std::string Module = "server";
    };

    Build build;
    std::map<std::string, Function> functions;
    std::map<std::string, Global> globals;
    std::map<std::string, VTable> vtables;
    std::map<std::string, Offset> offsets;
};

#ifdef _WIN32
inline constexpr std::string_view PlatformName = "windows";
#else
inline constexpr std::string_view PlatformName = "linux";
#endif

template <class TEntry>
const auto& PlatformColumn(const TEntry& entry)
{
#ifdef _WIN32
    return entry.Windows;
#else
    return entry.Linux;
#endif
}

}  // namespace VoltMod

// Explicit maps preserve JSON keys that differ from C++ member names.

template <>
struct glz::meta<VoltMod::GameDataDocument>
{
    static constexpr auto modify = glz::object("$schema", glz::skip{});
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Function>
{
    using T = VoltMod::GameDataDocument::Function;
    static constexpr auto value = glz::object("module", &T::Module, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Global>
{
    using T = VoltMod::GameDataDocument::Global;
    static constexpr auto value = glz::object("module", &T::Module, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::VTable>
{
    using T = VoltMod::GameDataDocument::VTable;
    static constexpr auto value = glz::object("class", &T::Class, "base", &T::Base, "module", &T::Module, "windows",
                                              &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Offset>
{
    using T = VoltMod::GameDataDocument::Offset;
    static constexpr auto value = glz::object("windows", &T::Windows, "linux", &T::Linux, "class", &T::Class, "base",
                                              &T::Base, "module", &T::Module);
};
