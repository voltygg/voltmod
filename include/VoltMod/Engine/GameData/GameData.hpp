#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/OriginalVfn.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Resolves one gamedata file against the loaded modules once per load.
 *
 * Gamedata supplies locations; @ref Bindings exposes their typed engine ABIs. @ref Load clears
 * previous results and resolves every entry eagerly, reporting failures by key at load time.
 */
class GameData
{
public:
    /** Section that produced a resolution, determining which fields of @ref Resolution apply. */
    enum class Kind : uint8_t
    {
        Signature,  ///< Address is the pattern match.
        Address,    ///< Address is the rel32 target derived from a signature match.
        VTable,     ///< Index is the slot, Class and Library name the table it is counted in.
        Offset      ///< Index is a validated byte offset.
    };

    /** Resolved value for one gamedata key, or its failure reason. */
    struct Resolution
    {
        Kind Section = Kind::Signature;
        void* Address = nullptr;  ///< Signature match, rel32 target, or the code a vtable slot holds.
        void* Table = nullptr;    ///< VTable located at load, so binding does not scan for it again.
        int Index = -1;           ///< VTable slot or byte offset.
        std::string Class;        ///< VTable class whose table owns the slot.
        std::string Library;      ///< Module containing a signature or vtable.
        std::string Error;        ///< Empty when resolved.
    };

    GameData() = default;
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;

    /** Every gamedata key with what it resolved to, keyed by the name the file gives it. */
    using ResolutionMap = std::map<std::string, Resolution>;

    /**
     * Parse @p path and resolve every entry against the loaded modules.
     *
     * Clears previous state before resolving, so reloads cannot retain stale results. Resolution
     * failures are recorded by key; only a missing or malformed file fails the load.
     */
    Status Load(std::string_view path, const OriginalVfn& originalOf = {});

    /** Every key, resolved or not. Used by diagnostics and @ref Bindings::Bind. */
    const ResolutionMap& Resolutions() const { return _resolved; }

    /** Number of entries resolved from @p kind. */
    size_t CountOf(Kind kind) const;

    /** Returns `N/M entries failed: a, b`, or empty when all entries resolved. */
    std::string FailureSummary() const;

    /** Date recorded by the gamedata file as its last verification against the game. */
    std::string_view VerifiedOn() const { return _verified; }

private:
    ResolutionMap _resolved;
    std::string _verified;
};

}  // namespace VoltMod
