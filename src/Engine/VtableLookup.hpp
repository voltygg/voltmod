#pragma once

#include <optional>

namespace VoltMod
{

/**
 * Address of `className`'s virtual function table inside the loaded module `moduleName`
 * ("engine2" -> engine2.dll / libengine2.so), or nullptr when it cannot be resolved.
 *
 * The returned pointer is what an instance of the class carries in its vptr slot, i.e. what
 * SourceHook's DVP hooks expect. Both platforms go through the compiler's own class metadata
 * rather than a byte signature, so the lookup survives code changes - but it is still best-effort
 * and callers must degrade gracefully on nullptr:
 *
 * - Windows walks MSVC RTTI: the mangled type descriptor in `.data`, the complete object locator
 *   in `.rdata` that names it, then the vtable slot holding that locator. Only the top-level,
 *   non-template `class` decoration `.?AV<name>@@` is matched, so a `struct` (`.?AU`), a nested
 *   class, or a namespaced one resolves to nullptr. The walk also accepts a locator only at
 *   offset 0, i.e. the primary vtable, never a base's subobject table.
 * - Linux reads the module's on-disk ELF symbol tables for the Itanium ABI `_ZTV<len><name>`
 *   symbol, in `.symtab` or `.dynsym`. A fully stripped library has neither and resolves to
 *   nullptr.
 */
void* FindVirtualTable(const char* moduleName, const char* className);

/** Where a virtual function was found in an object's vtables. See @ref FindVTableSlot. */
struct VTableSlot
{
    void* Table = nullptr;  ///< the vtable holding the function, as a hook binds to it
    int Index = 0;          ///< slot within that table
    int BaseOffset = 0;     ///< bytes from the object to the subobject that owns Table
};

/**
 * Locate @p function among the vtables an instance carries, by searching for its address.
 *
 * @ref FindVirtualTable answers only for a primary vtable, so a virtual inherited from a
 * secondary base is out of its reach: `CServerSideClient::FilterMessage` comes from the third
 * base of `CServerSideClientBase` and is not in the class's primary table at any index. This
 * finds it the other way round - from the function's address, which a byte signature already
 * gives us - and returns the table a DVP hook binds to plus the index to reconfigure it at.
 *
 * Because the address is the search key, the result cannot be off by one: either the slot holds
 * exactly @p function or it is not the slot. There is no index in gamedata to drift.
 *
 * @param instance a live object of the class; its leading pointer-sized slots are treated as
 *                 candidate vptrs, which covers a handful of bases.
 * @return the slot, or nothing when @p function is in none of them.
 *
 * @note @p BaseOffset is what a handler needs to get back to the object: a DVP hook on a
 *       secondary table is called with the *subobject* pointer, so a field at a known offset
 *       from the object lives at `self - BaseOffset + offset`.
 */
std::optional<VTableSlot> FindVTableSlot(const void* instance, const void* function);

}  // namespace VoltMod
