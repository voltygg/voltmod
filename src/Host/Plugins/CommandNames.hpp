#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Which plugin owns each console command name, so two plugins cannot register the same one. */
class CommandNames
{
public:
    /** The opaque pointer the host identifies a plugin by. */
    using OwnerId = const void*;

    /** False when another owner already has @p name. @p ownerName is what OwnerOf answers with. */
    bool Register(OwnerId owner, std::string_view ownerName, std::string_view name);

    /** Keep @p name for the host's own console commands. */
    void RegisterForHost(std::string_view name);

    /** The owner's name, "the host" for the host's own, empty while @p name is free. */
    std::string_view OwnerOf(std::string_view name) const;

    /** Remove every name @p owner registered. A plugin has no other way to give one back. */
    void RemoveAll(OwnerId owner);

private:
    struct Entry
    {
        std::string Name;
        OwnerId Owner = nullptr;  ///< nullptr is the host
        std::string OwnerName;
    };

    std::vector<Entry> _entries;
};

}  // namespace VoltMod
