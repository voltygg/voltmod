#pragma once

namespace VoltMod::Sdk
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

}  // namespace VoltMod::Sdk
