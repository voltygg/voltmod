#pragma once

#include "Engine/GameData/GameDataDocument.hpp"
#include "Engine/GameData/ResolvedRecord.hpp"
#include "Engine/Memory/LoadedModule.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/OriginalSlotLookup.hpp>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
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

/**
 * @brief Resolve gamedata keys against loaded modules.
 *
 * An unresolved key yields an empty value and adds `key: reason` to @ref Failures.
 */
class GameDataResolver
{
public:
    GameDataResolver(const GameDataDocument& file, const OriginalSlotLookup& originalOf);

    void* Function(std::string_view key);
    void* FunctionOrGlobal(std::string_view key);
    VirtualSlot Slot(std::string_view key);
    int Offset(std::string_view key);

    const std::vector<std::string>& Failures() const { return _failures; }

    const ResolvedRecord& Record() const { return _record; }

    /** Log resolution counts, addresses, unused keys, and build mismatches. */
    void LogSummary(std::string_view path) const;

private:
    /** Mark @p key used and require it to appear in one of @p allowed sections. */
    Status UseKey(std::string_view key, std::initializer_list<std::string_view> allowed);

    Result<void*> FindFunction(const std::string& key);
    Result<void*> FindGlobal(const std::string& key);
    Result<VirtualSlot> FindSlot(const std::string& key);
    Result<int> FindOffset(const std::string& key);
    Result<int> FindBaseOffset(const std::string& key, const GameDataDocument::Offset& entry);

    /** Return @p resolved's value, or @p unbound after recording its failure. */
    template <class T>
    T Bind(std::string_view key, const Result<T>& resolved, std::type_identity_t<T> unbound = {});

    struct Entry
    {
        std::vector<std::string_view> Sections;
        bool Used = false;
    };

    const GameDataDocument& _file;
    const OriginalSlotLookup& _originalOf;
    std::map<std::string, Entry, std::less<>> _sections;
    ModuleCache _cache;
    std::vector<std::string> _slotAddresses;
    std::vector<std::string> _failures;
    ResolvedRecord _record;
};

}  // namespace VoltMod
