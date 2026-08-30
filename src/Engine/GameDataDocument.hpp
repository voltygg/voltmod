#pragma once

#include "Engine/GameDataFile.hpp"

#include <VoltMod/Core/Json.hpp>
#include <map>
#include <optional>
#include <string>

// gamedata.jsonc exactly as it is written, before any validation. Reflection maps public members
// onto JSON keys, so an unknown key is rejected here rather than silently ignored - which is
// gamedata.schema.json's `additionalProperties: false` finally being enforced by the parser.
//
// These are the document's shape; GameDataFile is the validated model the rest of the framework
// reads. They are nested so the document's vocabulary (`Signature`, `Address`, `Offset`) does not
// have to be qualified out of collision at VoltMod scope.

namespace VoltMod
{

struct GameDataDocument
{
    /** One platform's byte pattern, under `signatures.<key>.<platform>`. */
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

    /** `addresses.<key>.rel32At` carries the platform columns; the entry itself does not. */
    struct Rel32
    {
        std::optional<int> Windows;
        std::optional<int> Linux;
    };

    struct Address
    {
        std::string signature;
        std::optional<Rel32> rel32At;
    };

    struct VTable
    {
        std::string Class;
        std::string library = "server";
        std::optional<int> Windows;
        std::optional<int> Linux;
    };

    /** `messages`: a bare per-platform integer id. */
    struct Columns
    {
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

    /** Optional so an absent key reads as "no integer 'version'" rather than version 0. */
    std::optional<int> version;
    Build build;
    std::map<std::string, Signature> signatures;
    std::map<std::string, Address> addresses;
    std::map<std::string, VTable> vtables;
    std::map<std::string, Columns> messages;
    std::map<std::string, Offset> offsets;
};

}  // namespace VoltMod

// Explicit key maps for the members whose JSON key is a C++ keyword (`class`) or a name better not
// left to reflection (`windows`/`linux`). Each lists every member on purpose: a partial rename
// would leave the reflected spelling in place alongside the alias.

template <>
struct glz::meta<VoltMod::GameDataDocument::Signature>
{
    using T = VoltMod::GameDataDocument::Signature;
    static constexpr auto value = glz::object("library", &T::library, "windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::Rel32>
{
    using T = VoltMod::GameDataDocument::Rel32;
    static constexpr auto value = glz::object("windows", &T::Windows, "linux", &T::Linux);
};

template <>
struct glz::meta<VoltMod::GameDataDocument::VTable>
{
    using T = VoltMod::GameDataDocument::VTable;
    static constexpr auto value =
        glz::object("class", &T::Class, "library", &T::library, "windows", &T::Windows, "linux", &T::Linux);
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

/** gamedata.jsonc names its schema for editor completion, like every settings.jsonc. */
VOLTMOD_SETTINGS_ROOT(VoltMod::GameDataDocument)
