#pragma once

#include "Engine/Memory/ScriptBindings.hpp"
#include "Host/GameData/GameDataDocument.hpp"
#include "Host/GameData/ModuleCache.hpp"
#include "Host/GameData/ResolvedGameData.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/OriginalSlotLookup.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** A vtable slot and its class table. Index is -1 when unbound. */
struct VirtualSlot
{
    int Index = -1;
    void* Table = nullptr;
};

/** What one gamedata key resolved to. @ref Reason is empty exactly when it bound. */
struct ResolvedEntry
{
    /** Every section holding the key; more than one is a file error. */
    std::vector<std::string_view> Sections;
    /** No kind when the key is in more than one section. */
    GameDataSection Kind{};
    void* Address = nullptr;
    int Value = -1;
    std::string Reason;
};

/** Resolves every gamedata key against the loaded modules, once for the process. */
class GameDataResolver
{
public:
    /** @p file, @p originalOf and @p scriptOf must outlive the resolver. */
    GameDataResolver(const GameDataDocument& file, const OriginalSlotLookup& originalOf,
                     const ScriptBindingLookup& scriptOf);

    void ResolveAll();

    /** Nullptr when the file does not have @p key. */
    const ResolvedEntry* Find(std::string_view key) const;

    /** `key: reason` for each entry that did not bind. */
    const std::vector<std::string>& Failures() const { return _failures; }

    const ResolvedGameData& Resolved() const { return _resolved; }

    /** Log the entry counts, and warn when the file was checked on another game build. */
    void LogSummary(std::string_view path) const;

private:
    /** An address, a slot or offset value, or both for a vtable slot. */
    struct Bound
    {
        void* Address = nullptr;
        int Value = -1;
    };

    Result<Bound> Locate(GameDataSection kind, const std::string& key);
    void Fail(std::string_view key, ResolvedEntry& entry, const Error& error);

    Result<void*> FindFunction(const std::string& key);
    Result<void*> FindScriptFunction(const std::string& key, const GameDataDocument::Function& entry);
    Result<void*> FindGlobal(const std::string& key);
    Result<VirtualSlot> FindSlot(const std::string& key);
    Result<int> FindOffset(const std::string& key);
    Result<int> FindBaseOffset(const GameDataDocument::Offset& entry);

    Result<ScriptTarget> FindScript(void* table, const std::string& name) const;
    /** The slot a virtual VScript binding dispatches through. */
    Result<int> ScriptSlot(void* table, const std::string& name) const;

    const GameDataDocument& _file;
    const OriginalSlotLookup& _originalOf;
    const ScriptBindingLookup& _scriptOf;
    std::map<std::string, ResolvedEntry, std::less<>> _entries;
    ModuleCache _cache;
    std::vector<std::string> _failures;
    ResolvedGameData _resolved;
};

}  // namespace VoltMod
