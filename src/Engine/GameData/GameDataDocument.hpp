#pragma once

#include <VoltMod/Core/Json.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// gamedata.jsonc as read. Strict reflection rejects unknown keys, matching the schema's
// `additionalProperties: false`. Columns are `Windows`/`Linux` because GCC predefines `linux`.

namespace VoltMod
{

struct GameDataDocument
{
    struct Build
    {
        std::string server;    ///< steam.inf ServerVersion the entries were verified on.
        std::string verified;  ///< YYYY-MM-DD of the last full review.
    };

    /** A byte pattern matching the start of a function. */
    struct Function
    {
        std::string library = "server";
        std::optional<std::string> Windows;
        std::optional<std::string> Linux;
    };

    /** A pattern, and the byte distance from its match to a rel32 displacement pointing at the global. */
    struct GlobalColumn
    {
        std::string pattern;
        int rel32At = 0;
    };

    struct Global
    {
        std::string library = "server";
        std::optional<GlobalColumn> Windows;
        std::optional<GlobalColumn> Linux;
    };

    /** A slot counted in the primary vtable of `class`. */
    struct VTable
    {
        std::string Class;
        std::string library = "server";
        std::optional<int> Windows;
        std::optional<int> Linux;
    };

    struct Offset
    {
        std::optional<int> Windows;
        std::optional<int> Linux;
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

/** This platform's column of @p entry. */
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

// Explicit maps keep JSON keys that are C++ keywords or differ in case from member names.

template <>
struct glz::meta<VoltMod::GameDataDocument>
{
    static constexpr auto modify = glz::object("$schema", glz::skip{});
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Function>
{
    using T = VoltMod::GameDataDocument::Function;
    static constexpr auto value = glz::object("library", &T::library, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Global>
{
    using T = VoltMod::GameDataDocument::Global;
    static constexpr auto value = glz::object("library", &T::library, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::VTable>
{
    using T = VoltMod::GameDataDocument::VTable;
    static constexpr auto value =
        glz::object("class", &T::Class, "library", &T::library, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Offset>
{
    using T = VoltMod::GameDataDocument::Offset;
    static constexpr auto value = glz::object("windows", &T::Windows, "linux", &T::Linux);
};
