#pragma once

#include <string>
#include <unordered_map>

namespace VoltMod::Sdk
{

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
    int GetOffset(const std::string& name) const;
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

    std::unordered_map<std::string, int> _offsets;
    std::unordered_map<std::string, SignatureEntry> _signatures;
    std::unordered_map<std::string, ResolvedEntry> _resolved;
};

}  // namespace VoltMod::Sdk
