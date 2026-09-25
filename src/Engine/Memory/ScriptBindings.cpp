#include "Engine/Memory/ScriptBindings.hpp"

#include "Engine/Memory/SigScanner.hpp"

#include <cstdint>
#include <cstring>
#include <entity2/entityinstance.h>
#include <format>
#include <khook.hpp>
#include <vscript/ivscript.h>

namespace VoltMod
{

// The SDK's ScriptFunctionBinding_t predates the class pointer CS2 added after the descriptor.
struct ScriptFunctionBinding
{
    ScriptFuncDescriptor_t Descriptor;
    ScriptClassDesc_t* Class;
    ScriptBindingFunc_t Binding;
    void* Function;
    unsigned Flags;
};

static const ScriptFunctionBinding* FindBinding(const ScriptClassDesc_t* description, std::string_view name)
{
    for (; description; description = description->m_pBaseDesc)
    {
        const auto& bindings =
            reinterpret_cast<const CUtlVector<ScriptFunctionBinding>&>(description->m_FunctionBindings);
        for (int i = 0; i < bindings.Count(); ++i)
        {
            const char* scriptName = bindings[i].Descriptor.m_pszScriptName;
            if (scriptName && name == scriptName)
            {
                return &bindings[i];
            }
        }
    }
    return nullptr;
}

/** The vtable index @p function dispatches through, or -1 when it is not virtual. */
static int VirtualIndex(const void* function)
{
#ifdef _WIN32
    // MSVC points a virtual member at a thunk: mov rax, [rcx]; jmp [rax + disp].
    const auto* code = static_cast<const uint8_t*>(function);
    if (!IsReadableAddress(code, 9) || code[0] != 0x48 || code[1] != 0x8B || code[2] != 0x01 || code[3] != 0xFF)
    {
        return -1;
    }
    switch (code[4])
    {
    case 0x20:
        return 0;
    case 0x60:
        return static_cast<int8_t>(code[5]) / static_cast<int>(sizeof(void*));
    case 0xA0:
    {
        int32_t displacement = 0;
        std::memcpy(&displacement, code + 5, sizeof(displacement));
        return displacement / static_cast<int>(sizeof(void*));
    }
    default:
        return -1;
    }
#else
    // Itanium stores a virtual member as one plus its byte offset in the table.
    const auto value = reinterpret_cast<uintptr_t>(function);
    return (value & 1) != 0 ? static_cast<int>((value - 1) / sizeof(void*)) : -1;
#endif
}

Result<ScriptTarget> FindScriptBinding(void* classTable, std::string_view name)
{
    // Call the slot, not a virtual on a fake object: GCC drops that as undefined. `this` is ignored.
    using GetScriptDescFn = void* (*)(void* self);
    const int slot = KHook::GetVtableIndex(&CEntityInstance::GetScriptDesc);
    if (slot < 0)
    {
        return std::unexpected(Error::NotFound("no vtable slot for GetScriptDesc"));
    }
    const auto getScriptDesc = reinterpret_cast<GetScriptDescFn>(static_cast<void**>(classTable)[slot]);
    const auto* description = static_cast<const ScriptClassDesc_t*>(getScriptDesc(&classTable));
    if (!description)
    {
        return std::unexpected(Error::NotFound("the class has no VScript description"));
    }

    const ScriptFunctionBinding* binding = FindBinding(description, name);
    if (!binding || !binding->Function)
    {
        return std::unexpected(Error::NotFound(std::format("no VScript binding '{}'", name)));
    }

    const int index = VirtualIndex(binding->Function);
    return index >= 0 ? ScriptTarget{.Index = index} : ScriptTarget{.Address = binding->Function};
}

}  // namespace VoltMod
