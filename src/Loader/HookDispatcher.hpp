#pragma once

#include <cstddef>
#include <khook.hpp>

namespace VoltMod
{

/**
 * @brief The loader's KHook, which every module's hooks go through.
 *
 * No per-module bookkeeping: each hook is removed by the subscription that owns it.
 */
class HookDispatcher final : public KHook::IKHook
{
public:
    KHook::HookID_t SetupHook(void* function, void* context, void* removedFunction, void* pre, void* post,
                              void* makeReturn, void* makeCallOriginal, unsigned int stackSize, bool async) override
    {
        return KHook::SetupHook(function, context, removedFunction, pre, post, makeReturn, makeCallOriginal, stackSize,
                                async);
    }

    KHook::HookID_t SetupVirtualHook(void** vtable, int index, void* context, void* removedFunction, void* pre,
                                     void* post, void* makeReturn, void* makeCallOriginal, unsigned int stackSize,
                                     bool async) override
    {
        return KHook::SetupVirtualHook(vtable, index, context, removedFunction, pre, post, makeReturn, makeCallOriginal,
                                       stackSize, async);
    }

    void RemoveHook(KHook::HookID_t id, bool async, void (*removed)(KHook::HookID_t, void*), void* context) override
    {
        KHook::RemoveHook(id, async, removed, context);
    }

    void* GetContextPtr() override { return KHook::GetContextPtr(); }
    void* GetOriginalFunction() override { return KHook::GetOriginalFunction(); }
    void* GetOriginalValuePtr() override { return KHook::GetOriginalValuePtr(); }
    void* GetOverrideValuePtr() override { return KHook::GetOverrideValuePtr(); }
    void* GetCurrentValuePtr(bool pop) override { return KHook::GetCurrentValuePtr(pop); }
    void DestroyReturnValue() override { KHook::DestroyReturnValue(); }
    void* FindOriginal(void* function) override { return KHook::FindOriginal(function); }
    void* FindOriginalVirtual(void** vtable, int index) override { return KHook::FindOriginalVirtual(vtable, index); }

    void* DoRecall(KHook::Action action, void* returnValue, std::size_t returnSize, void* construct,
                   void* destruct) override
    {
        return KHook::DoRecall(action, returnValue, returnSize, construct, destruct);
    }

    void SaveReturnValue(KHook::Action action, void* returnValue, std::size_t returnSize, void* construct,
                         void* destruct, bool original) override
    {
        KHook::SaveReturnValue(action, returnValue, returnSize, construct, destruct, original);
    }

    void* LookupSignature(void* start, std::size_t size, const char* signature) override
    {
        return KHook::LookupSignature(start, size, signature);
    }

    bool WasOriginalFunctionSkipped() override { return KHook::WasOriginalFunctionSkipped(); }
};

}  // namespace VoltMod
