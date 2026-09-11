#pragma once

#include "Engine/GameDataFile.hpp"

#include <VoltMod/Core/Json.hpp>
#include <map>
#include <optional>
#include <string>

// This mirrors gamedata.jsonc before validation. Strict reflection rejects unknown keys, matching
// gamedata.schema.json's `additionalProperties: false`; nested types keep document names local.

namespace VoltMod
{

struct GameDataDocument
{
    /** One platform's byte pattern under `signatures.<key>.<platform>`. */
    struct Pattern
    {
        std::string pattern;
    };

    struct Signature
    {
        std::string library = "server";
        std::optional<Pattern> Windows;
        std::optional<Pattern> Linux;
    };

    /** Per-platform integer columns for `addresses.<key>.rel32At`. */
    struct Columns
    {
        std::optional<int> Windows;
        std::optional<int> Linux;
    };

    struct Address
    {
        std::string signature;
        std::optional<Columns> rel32At;
    };

    struct VTable
    {
        std::string Class;
        std::string library = "server";
        std::string signature;
        std::optional<int> Windows;
        std::optional<int> Linux;
    };

    struct Offset
    {
        std::optional<int> Windows;
        std::optional<int> Linux;
        int max = MaxByteOffset;
        int align = 1;
    };

    struct Build
    {
        std::string game;
        std::string verified;
        std::string note;
    };

    Build build;
    std::map<std::string, Signature> signatures;
    std::map<std::string, Address> addresses;
    std::map<std::string, VTable> vtables;
    std::map<std::string, Offset> offsets;
};

}  // namespace VoltMod

// Explicit maps preserve JSON keys that are C++ keywords or differ in case from member names.
// Map every member because partial maps leave reflected aliases active.

template <>
struct glz::meta<VoltMod::GameDataDocument>
{
    static constexpr auto modify = glz::object("$schema", glz::skip{});
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Signature>
{
    using T = VoltMod::GameDataDocument::Signature;
    static constexpr auto value = glz::object("library", &T::library, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::VTable>
{
    using T = VoltMod::GameDataDocument::VTable;
    static constexpr auto value = glz::object("class", &T::Class, "library", &T::library, "signature", &T::signature,
                                              "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Columns>
{
    using T = VoltMod::GameDataDocument::Columns;
    static constexpr auto value = glz::object("windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Offset>
{
    using T = VoltMod::GameDataDocument::Offset;
    static constexpr auto value =
        glz::object("windows", &T::Windows, "linux", &T::Linux, "max", &T::max, "align", &T::align);
};
