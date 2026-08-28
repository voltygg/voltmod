#pragma once

#include <VoltMod/Core/Result.hpp>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Which platform's column of a gamedata entry to keep. Selected once, at parse time. */
enum class GamePlatform
{
    Windows,
    Linux
};

#ifdef _WIN32
inline constexpr GamePlatform HostPlatform = GamePlatform::Windows;
#else
inline constexpr GamePlatform HostPlatform = GamePlatform::Linux;
#endif

/** The only `version` this parser accepts. A bump is a breaking format change, never a fallback. */
inline constexpr int GameDataFormatVersion = 2;

/** Ceilings far above every real value that still catch drifted or hand-edited gamedata. */
inline constexpr int MaxVtableIndex = 500;
inline constexpr int MaxByteOffset = 4096;

/** Provenance of one gamedata file, for diagnostics and the re-verification procedure. */
struct GameDataBuild
{
    std::string Game;
    std::string Verified;  ///< YYYY-MM-DD the file was last walked entry by entry.
    std::string Note;
};

/** A byte pattern to scan for in one module. */
struct SignatureEntry
{
    std::string Library = "server";
    std::string Pattern;
};

/** A pointer reached through a rel32 displacement inside a matched signature. */
struct AddressEntry
{
    std::string Signature;  ///< Key in GameDataFile::Signatures this derives from.
    int Rel32At = 0;        ///< Byte distance from the match to the 4-byte displacement.
};

/** One virtual function table slot, and the class whose table the index is counted in. */
struct VTableEntry
{
    std::string Class;
    std::string Library = "server";
    int Index = -1;
};

/** A byte offset into a layout the SDK does not declare, with the bounds it must satisfy. */
struct OffsetEntry
{
    int Value = -1;
    int Max = MaxByteOffset;
    int Align = 1;
};

/**
 * @brief One parsed gamedata file: plain data, already narrowed to the host platform.
 *
 * Parsing is separated from scanning so the format - versioning, cross-section key collisions,
 * bounds, and the signature references `addresses` depends on - can be checked with no engine
 * loaded. @ref GameData consumes the result and resolves it against live module memory.
 */
struct GameDataFile
{
    int Version = 0;
    GameDataBuild Build;
    std::map<std::string, SignatureEntry> Signatures;
    std::map<std::string, AddressEntry> Addresses;
    std::map<std::string, VTableEntry> VTables;
    std::map<std::string, OffsetEntry> Offsets;
    /** Network message type ids, keyed the same way; see the section's schema description. */
    std::map<std::string, int> Messages;

    /** Keys the file carries for the other platform only, so they are absent from the maps above.
     *  gamedata.schema.json requires one platform column, not both, and a feature that has only
     *  been located on one of them is a capability that is off there - not a malformed file.
     *  Naming them keeps that distinct from a key nobody wrote. */
    std::vector<std::string> OtherPlatformOnly;

    /** Total entries across every section; each one is a key @ref Bindings can name. */
    size_t EntryCount() const
    {
        return Signatures.size() + Addresses.size() + VTables.size() + Offsets.size() + Messages.size();
    }

    /**
     * Parse @p text (JSONC) keeping @p platform's column of every entry.
     *
     * An entry carrying only the *other* platform's column is not an error: its key is recorded in
     * @ref OtherPlatformOnly and left out of the maps, so it resolves as absent.
     *
     * @return Error::Invalid naming the offending key for: a missing or unsupported `version`, a
     *         key used in more than one section, an entry with no column for either platform, a
     *         malformed pattern, a negative `rel32At`, an `addresses` entry naming a signature
     *         that does not exist, a vtable index outside [0, @ref MaxVtableIndex), or an offset
     *         above its `max` or not a multiple of its `align`.
     */
    static Result<GameDataFile> Parse(std::string_view text, GamePlatform platform);

    /** @ref Parse over a file, with the path resolved through ResolvePath. */
    static Result<GameDataFile> Load(std::string_view path, GamePlatform platform);
};

/** True when @p pattern is space-separated hex bytes and `?`/`??` wildcards, and not empty. */
bool IsValidBytePattern(std::string_view pattern);

}  // namespace VoltMod
