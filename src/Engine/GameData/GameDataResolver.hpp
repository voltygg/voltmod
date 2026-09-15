#pragma once

#include "Engine/GameData/GameDataDocument.hpp"
#include "Engine/GameData/ResolvedRecord.hpp"
#include "Engine/Memory/LoadedModule.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/OriginalVfn.hpp>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace VoltMod
{

/** A vtable slot and the class table it is counted in; Index is -1 when unbound. */
struct VirtualSlot
{
    int Index = -1;
    void* Table = nullptr;
};

/**
 * @brief Resolves gamedata keys against the loaded modules, one key at a time.
 *
 * A key that does not resolve yields an empty value and adds `key: reason` to @ref Failures.
 * Modules, class tables and base classes are found once and shared by every key that names them.
 */
class GameDataResolver
{
public:
    GameDataResolver(const GameDataDocument& file, const OriginalVfn& originalOf);

    void* Function(std::string_view key);
    /** A function or a global, whichever section holds @p key. */
    void* FunctionOrGlobal(std::string_view key);
    VirtualSlot Slot(std::string_view key);
    int Offset(std::string_view key);

    const std::vector<std::string>& Failures() const { return _failures; }

    /** Module-relative addresses of everything that resolved. */
    const ResolvedRecord& Record() const { return _record; }

    /** Log the file's counts, where vtable slots point, RTTI offsets, unused keys, and a build mismatch. */
    void LogSummary(std::string_view path) const;

private:
    /** The section @p key is in, when that is exactly one of @p readable. */
    Result<std::string_view> Claim(std::string_view key, std::initializer_list<std::string_view> readable);

    Result<void*> FindFunction(const std::string& key);
    Result<void*> FindGlobal(const std::string& key);
    Result<VirtualSlot> FindSlot(const std::string& key);
    Result<int> FindOffset(const std::string& key);
    Result<int> FindBaseOffset(const std::string& key, const GameDataDocument::Offset& entry);

    /** Each module is looked up once. Null when it is not loaded. */
    const LoadedModule* FindModule(const std::string& moduleName);
    /** One table per module and class; several slots share it. */
    void* Table(const LoadedModule& loaded, const std::string& moduleName, const std::string& className);
    /** Where @p baseName sits in @p className, looked up once per module, class and base. */
    Result<BaseSubobject> FindBase(const LoadedModule& loaded, const std::string& moduleName,
                                   const std::string& className, const std::string& baseName);

    /** @p resolved's value, or @p unbound after recording why @p key did not resolve. */
    template <class T>
    T Keep(std::string_view key, const Result<T>& resolved, std::type_identity_t<T> unbound = {});

    const GameDataDocument& _file;
    const OriginalVfn& _originalOf;
    std::map<std::string, std::vector<std::string_view>, std::less<>> _sections;
    std::set<std::string, std::less<>> _used;
    std::map<std::string, LoadedModule> _modules;
    std::map<std::pair<std::string, std::string>, void*> _tables;
    std::map<std::tuple<std::string, std::string, std::string>, Result<BaseSubobject>> _bases;
    std::vector<std::string> _slotAddresses;
    std::vector<std::string> _baseOffsets;
    std::vector<std::string> _failures;
    ResolvedRecord _record;
};

}  // namespace VoltMod
