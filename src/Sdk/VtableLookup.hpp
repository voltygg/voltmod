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
 *   in `.rdata` that names it, then the vtable slot holding that locator.
 * - Linux reads the module's on-disk ELF symbol tables for the Itanium ABI `_ZTV<len><name>`
 *   symbol. A fully stripped library has no such symbol and resolves to nullptr.
 */
void* FindVirtualTable(const char* moduleName, const char* className);

}  // namespace VoltMod::Sdk
