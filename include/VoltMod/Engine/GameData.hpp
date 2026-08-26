#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace VoltMod::Engine
{

/** Ceilings far above every real value (today: index 390, offset 576) that still catch drifted or hand-edited gamedata.
 */
inline constexpr int MaxVtableIndex = 500;
inline constexpr int MaxByteOffset = 4096;

/**
 * @brief Centralized gamedata manager for platform-specific signatures and offsets.
 *
 * Loads byte-pattern signatures and named integer offsets from JSON.
 * Runtime::Start calls ResolveAll() during the GameData stage; later lookups use
 * its cache. Resolution results record missing and ambiguous patterns by name.
 */
class GameData
{
public:
    struct ResolvedEntry
    {
        void* Match = nullptr;     ///< Raw pattern-match address.
        void* Resolved = nullptr;  ///< Match after rel32 resolution (== Match when offset is 0).
        bool Unique = true;        ///< False when the pattern matched more than once.
        std::string Error;         ///< Empty when resolved.
    };

    GameData() = default;

    bool Load(const std::string& path);

    /** Vtable index for @p name, or -1 when missing or above @p maxIndex. Warns once either way. */
    int GetVtableIndex(std::string_view name, int maxIndex = MaxVtableIndex) const;
    /** Byte offset for @p name, or -1 when missing, above @p maxBytes, or not a multiple of @p alignment. Warns once
     * either way. */
    int GetByteOffset(std::string_view name, int maxBytes = MaxByteOffset, int alignment = 1) const;

    void* FindSignature(const std::string& name) const;
    void* ResolveSignature(const std::string& name) const;

    /** @brief Eagerly resolve every signature into the cache. */
    void ResolveAll();

    /** @brief "N/M signatures failed: a, b; ambiguous: c" - empty when all resolved uniquely. */
    std::string FailureSummary() const;

    const std::unordered_map<std::string, ResolvedEntry>& Resolutions() const { return _resolved; }
    size_t OffsetCount() const { return _offsets.size(); }
    size_t SignatureCount() const { return _signatures.size(); }

private:
    struct SignatureEntry
    {
        std::string Library;
        std::string Pattern;
        int Offset = 0;
    };

    /** Heterogeneous hashing, so a string_view lookup does not allocate a key. */
    struct StringHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };

    /** Offset for @p name, or nullopt after warning that it is missing. */
    std::optional<int> Lookup(std::string_view name) const;

    std::unordered_map<std::string, int, StringHash, std::equal_to<>> _offsets;
    std::unordered_map<std::string, SignatureEntry> _signatures;
    std::unordered_map<std::string, ResolvedEntry> _resolved;
};

}  // namespace VoltMod::Engine
