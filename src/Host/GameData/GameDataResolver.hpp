#pragma once

#include "Engine/Memory/LoadedModule.hpp"
#include "Engine/Memory/VtableLookup.hpp"
#include "Host/GameData/GameDataDocument.hpp"
#include "Host/GameData/ResolvedGameData.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/OriginalSlotLookup.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace VoltMod
{

/** A vtable slot and its class table. Index is -1 when unbound. */
struct VirtualSlot
{
    int Index = -1;
    void* Table = nullptr;
};

/**
 * @brief Locates modules, class vtables, and base subobjects, caching each lookup.
 */
class ModuleCache
{
public:
    const LoadedModule* Module(const std::string& moduleName);
    void* ClassTable(const LoadedModule& module, const std::string& moduleName, const std::string& className);
    Result<BaseSubobject> Base(const LoadedModule& module, const std::string& moduleName, const std::string& className,
                               const std::string& baseName);

private:
    std::map<std::string, LoadedModule> _modules;
    std::map<std::pair<std::string, std::string>, void*> _tables;
    std::map<std::tuple<std::string, std::string, std::string>, Result<BaseSubobject>> _bases;
};

/** What one gamedata key resolved to. @ref Reason is empty exactly when it bound. */
struct ResolvedEntry
{
    /** The sections holding the key: one normally, more when the file repeats it. */
    std::vector<std::string_view> Sections;
    /** The section's kind, or no kind at all when the key is in more than one. */
    GameDataSection Kind{};
    void* Address = nullptr;
    int Value = -1;
    std::string Reason;
};

/**
 * @brief Resolve every gamedata key against the loaded modules.
 *
 * An unresolved key keeps its reason and adds `key: reason` to @ref Failures.
 */
class GameDataResolver
{
public:
    /** @p file and @p originalOf must outlive the resolver. */
    GameDataResolver(const GameDataDocument& file, const OriginalSlotLookup& originalOf);

    /** Scan for every entry in the file. Called once: this is the work the host does for all. */
    void ResolveAll();

    /** What @p key resolved to, or nullptr when the file does not have it. */
    const ResolvedEntry* Find(std::string_view key) const;

    const std::vector<std::string>& Failures() const { return _failures; }

    const ResolvedGameData& Resolved() const { return _resolved; }

    /** Log resolution counts, addresses, and build mismatches. */
    void LogSummary(std::string_view path) const;

private:
    void Resolve(const std::string& key, ResolvedEntry& entry);

    /** Keep @p error as @p entry's reason and name the entry in @ref Failures. */
    void Fail(std::string_view key, ResolvedEntry& entry, const Error& error);

    Result<void*> FindFunction(const std::string& key);
    Result<void*> FindGlobal(const std::string& key);
    Result<VirtualSlot> FindSlot(const std::string& key);
    Result<int> FindOffset(const std::string& key);
    Result<int> FindBaseOffset(const std::string& key, const GameDataDocument::Offset& entry);

    const GameDataDocument& _file;
    const OriginalSlotLookup& _originalOf;
    std::map<std::string, ResolvedEntry, std::less<>> _entries;
    ModuleCache _cache;
    std::vector<std::string> _failures;
    ResolvedGameData _resolved;
};

}  // namespace VoltMod
