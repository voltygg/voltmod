#pragma once

#include <VoltMod/Core/Result.hpp>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Platform column retained during parsing. */
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

/** Bounds that catch drifted or hand-edited gamedata. */
inline constexpr int MaxVtableIndex = 500;
inline constexpr int MaxByteOffset = 4096;

/** Gamedata provenance used for diagnostics and re-verification. */
struct GameDataBuild
{
    std::string Game;
    std::string Verified;  ///< YYYY-MM-DD date of the last entry-by-entry verification.
    std::string Note;
};

/** Byte pattern to scan in one module. */
struct SignatureEntry
{
    std::string Library = "server";
    std::string Pattern;
};

/** Pointer reached through a rel32 displacement in a matched signature. */
struct AddressEntry
{
    std::string Signature;  ///< Key in GameDataFile::Signatures used as the base match.
    int Rel32At = 0;        ///< Byte distance to the 4-byte displacement.
};

/** Virtual function table slot and the class whose table owns its index. */
struct VTableEntry
{
    std::string Class;
    std::string Library = "server";
    int Index = -1;         ///< Fallback when Signature is empty or absent from the table.
    std::string Signature;  ///< Key in GameDataFile::Signatures naming this slot's function.
};

/** Byte offset into an SDK-undeclared layout, with validation bounds. */
struct OffsetEntry
{
    int Value = -1;
    int Max = MaxByteOffset;
    int Align = 1;
};

/**
 * @brief One parsed gamedata file: plain data, already narrowed to the host platform.
 *
 * Parsing is separate from scanning so cross-section keys, bounds, and signature references can be
 * checked without a loaded engine. @ref GameData resolves the result against live module memory.
 */
struct GameDataFile
{
    GameDataBuild Build;
    std::map<std::string, SignatureEntry> Signatures;
    std::map<std::string, AddressEntry> Addresses;
    std::map<std::string, VTableEntry> VTables;
    std::map<std::string, OffsetEntry> Offsets;

    /** Keys present only for the other platform. Keeping their names distinguishes unavailable
     *  entries from missing ones. */
    std::vector<std::string> OtherPlatformOnly;

    /** Total entries across all sections. */
    size_t EntryCount() const { return Signatures.size() + Addresses.size() + VTables.size() + Offsets.size(); }

    /**
     * Parse @p text (JSONC) keeping @p platform's column of every entry.
     *
     * An entry carrying only the *other* platform's column is recorded in @ref OtherPlatformOnly
     * and left out of the maps, so it resolves as absent.
     *
     * @return Error::Invalid naming the offending key for duplicate sections, missing platform
     *         columns, malformed patterns, negative `rel32At`, unknown signatures referenced by
     *         `addresses` or `vtables`, vtable indices outside [0, @ref MaxVtableIndex), or offsets
     *         above `max` or not aligned to `align`.
     */
    static Result<GameDataFile> Parse(std::string_view text, GamePlatform platform);

    /** Run @ref Parse after reading @p path through ResolvePath. */
    static Result<GameDataFile> Load(std::string_view path, GamePlatform platform);
};

/** Whether @p pattern is non-empty space-separated hex bytes with `?`/`??` wildcards. */
bool IsValidBytePattern(std::string_view pattern);

}  // namespace VoltMod
