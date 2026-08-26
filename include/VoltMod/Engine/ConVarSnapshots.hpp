#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Engine
{

/**
 * @brief The values @ref ConVarLease saved before it took each convar over.
 *
 * Split out from the engine-facing shell because this is the part that is easy to get subtly
 * wrong - save on the first take only, hand back in the order taken - and it needs no running
 * server to check.
 *
 * Names and values are owned copies, so a caller may pass a temporary and nothing here outlives
 * its own storage. Values are held as strings, which is what the console path takes anyway and
 * what lets one set of snapshots cover float, int and bool convars without caring which is which.
 */
class ConVarSnapshots
{
public:
    struct Entry
    {
        std::string Name;
        std::string Value;  // the operator's value, as read before the first write
    };

    /**
     * Remember @p value as what @p name should go back to.
     *
     * @return false when @p name already has a snapshot, leaving the existing one untouched -
     *         which is what makes a repeat take a re-assert rather than a re-save.
     */
    bool Save(std::string_view name, std::string_view value);

    /** Drop @p name and hand back its saved value, or nullopt when nothing was saved for it. */
    std::optional<std::string> Remove(std::string_view name);

    bool Contains(std::string_view name) const;

    /** Every snapshot, oldest first. */
    const std::vector<Entry>& Entries() const { return _entries; }

    std::size_t Size() const { return _entries.size(); }
    void Clear() { _entries.clear(); }

private:
    std::vector<Entry>::const_iterator Find(std::string_view name) const;

    // Linear scan: a feature holds a handful of convars, never enough to index.
    std::vector<Entry> _entries;
};

}  // namespace VoltMod::Engine
