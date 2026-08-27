#pragma once

#include <VoltMod/Core/Result.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Resolves one gamedata file against the loaded modules, once per load.
 *
 * The file says *where* things are (a byte pattern, a rel32 displacement, a vtable slot, a field
 * offset); C++ owns *what* they are. @ref Bindings is the typed view built on top of this, and is
 * what services take - nothing looks an entry up by string on a call path.
 *
 * @ref Load parses, clears every previous result, and resolves every entry eagerly, so a failure
 * is a load-time diagnostic naming the key rather than a null pointer discovered later.
 */
class GameData
{
public:
    /** Which section an entry came from, and therefore which fields of @ref Resolution matter. */
    enum class Kind : uint8_t
    {
        Signature,  ///< Address is the pattern match.
        Address,    ///< Address is the rel32 target derived from a signature match.
        VTable,     ///< Index is the slot, Class and Library name the table it is counted in.
        Offset      ///< Index is a validated byte offset.
    };

    /** What one gamedata key resolved to, or why it did not. */
    struct Resolution
    {
        Kind Section = Kind::Signature;
        void* Address = nullptr;  ///< Signature match, or rel32 target.
        int Index = -1;           ///< VTable: slot index. Offset: byte offset.
        std::string Class;        ///< VTable: the RTTI/ELF class the table belongs to.
        std::string Library;      ///< Signature and VTable: the module to look in.
        std::string Error;        ///< Empty when the entry resolved.
    };

    GameData() = default;
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;

    /**
     * Parse @p path and resolve every entry against the loaded modules.
     *
     * Clears all previous state first, so a reload cannot leave a stale resolution behind. An
     * entry that fails to resolve is recorded with its reason and does not fail the load; only a
     * missing or malformed file does.
     */
    Status Load(const std::string& path);

    /** Every key, resolved or not. Diagnostics and @ref Bindings::Bind read this. */
    const std::map<std::string, Resolution>& Resolutions() const { return _resolved; }

    /** How many entries came from @p kind's section. */
    size_t CountOf(Kind kind) const;

    /** "N/M entries failed: a, b" - empty when every entry resolved. */
    std::string FailureSummary() const;

    /** The date the loaded file says its entries were last verified against the game. */
    std::string_view VerifiedOn() const { return _verified; }

private:
    std::map<std::string, Resolution> _resolved;
    std::string _game;
    std::string _verified;
};

}  // namespace VoltMod
