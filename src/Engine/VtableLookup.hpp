#pragma once

#include <functional>
#include <optional>

namespace VoltMod
{

/**
 * `className`'s primary vtable inside the loaded module `moduleName` ("engine2" -> engine2.dll /
 * libengine2.so), as an instance carries it in its vptr, or nullptr. Found through the compiler's
 * class metadata rather than a byte signature: MSVC RTTI on Windows (top-level, non-template
 * classes only), the Itanium `_ZTV` symbol on Linux (absent from a stripped library).
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
 * Locate @p function among the vtables a live @p instance carries, by address. Reaches virtuals a
 * secondary base contributes (`CServerSideClient::FilterMessage`), which @ref FindVirtualTable's
 * primary table never holds, and cannot be off by one: the slot holds the address or it is not
 * the slot.
 *
 * @p instance is walked blind: its first eight words are each tried as a vptr, and each candidate
 * table is walked until a slot holds something that is not code. Both are reads of foreign memory
 * whose extent this cannot know, so it requires a real engine object - one at least eight words
 * long, whose vtables are followed by readable non-executable data, which every module's `.rdata`
 * satisfies. Do not call it on a small stack struct; nothing here can tell that apart from an
 * object that simply has fewer bases.
 *
 * @param originalOf what an entry held before a hook patched it (SourceHook's
 *                   `GetOrigVfnPtrEntry`), or nullptr; without it another plugin's hook hides
 *                   @p function. Injected to keep this file SDK-free.
 * @return the slot, or nothing. A hook on a secondary table is called with the subobject, so
 *         `BaseOffset` is what a handler subtracts to reach the object.
 */
std::optional<VTableSlot> FindVTableSlot(const void* instance, const void* function,
                                         const std::function<const void*(void* entry)>& originalOf = {});

}  // namespace VoltMod
