#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <format>
#include <memory>
#include <string_view>
#include <type_traits>

namespace VoltMod
{
namespace Detail
{

/** Keep @p hook alive until the Subscription drops, then stop dispatch and tear it down. */
template <class Hook>
Subscription OwnHook(std::shared_ptr<Hook> hook)
{
    return Subscription([hook = std::move(hook)] { hook->ClearHooks(); });
}

}  // namespace Detail

/**
 * @brief Hook one virtual method of an engine interface for the Subscription's lifetime.
 *
 * KHook reads the vtable slot straight from @p method, so nothing has to be declared at namespace
 * scope. A handler is a member of @p self, takes the hooked object as its first parameter, and
 * returns `KHook::Return<Ret>`; pass `nullptr` for a side you do not want. Only @p instance is
 * hooked, not every object sharing its vtable.
 *
 * @code
 * KHook::Return<void> MyPlugin::OnGameFrame(IServerGameDLL*, bool simulating, bool, bool)
 * {
 *     return {KHook::Action::Ignore};
 * }
 *
 * void MyPlugin::OnRegisterHooks(VoltMod::Runtime& runtime, VoltMod::SubscriptionScope& hooks)
 * {
 *     hooks.Add(VoltMod::HookInterface(&IServerGameDLL::GameFrame, runtime.Unsafe.Interfaces.ServerGameDLL,
 *                                      this, nullptr, &MyPlugin::OnGameFrame));
 * }
 * @endcode
 */
template <class Iface, class Ret, class... Args, class Self, class Pre, class Post>
Subscription HookInterface(Ret (Iface::*method)(Args...), Iface* instance, Self* self, Pre pre, Post post)
{
    static_assert(!std::is_null_pointer_v<Pre> || !std::is_null_pointer_v<Post>,
                  "a hook with neither side would install nothing");

    auto hook = std::make_shared<KHook::Virtual<Iface, Ret, Args...>>(method, self, pre, post);
    hook->Add(instance);
    return Detail::OwnHook(std::move(hook));
}

/**
 * @brief Hook a gamedata-bound slot on every object sharing a class vtable.
 *
 * The slot comes from gamedata rather than from a member function pointer, so the handler names
 * its object as the opaque `VtableObject`. @p sampleInstance is only read to warn about a stale
 * class name; pass nullptr when no instance exists yet.
 */
template <class Ret, class... Args, class Self, class Pre, class Post>
Result<Subscription> HookVTable(std::string_view what, const VHookBinding<Ret(Args...)>& binding, Self* self, Pre pre,
                                Post post, void* sampleInstance = nullptr)
{
    static_assert(!std::is_null_pointer_v<Pre> || !std::is_null_pointer_v<Post>,
                  "a hook with neither side would install nothing");

    const VTableRef& table = binding.Table;
    const int index = binding.Method.Index();
    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", what)));
    if (!table)
        return std::unexpected(Error::Engine(std::format("the {} class vtable did not bind", what)));

    // A live instance detects a stale gamedata class name without blocking early installation.
    if (sampleInstance && *static_cast<void**>(sampleInstance) != table.Table())
        Log::Warn("{}: a live instance's vtable differs from {}; wrong class name?", what, table.Class());

    auto hook = std::make_shared<KHook::Virtual<VtableObject, Ret, Args...>>(index, self, pre, post);
    // KHook reads the vtable out of the object it is given, and the table is all we have.
    void* asObject = table.Table();
    hook->AddGlobal(reinterpret_cast<VtableObject*>(&asObject));

    Log::Info("{} hook installed on {} vtable (index {}).", what, table.Class(), index);
    return Detail::OwnHook(std::move(hook));
}

/** Hook a gamedata-bound slot on one object. Drop the Subscription before that object is destroyed. */
template <class Ret, class... Args, class Self, class Pre, class Post>
Result<Subscription> HookInstance(std::string_view what, void* instance, const VFn<Ret(Args...)>& method, Self* self,
                                  Pre pre, Post post)
{
    static_assert(!std::is_null_pointer_v<Pre> || !std::is_null_pointer_v<Post>,
                  "a hook with neither side would install nothing");

    const int index = method.Index();
    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", what)));
    if (!instance)
        return std::unexpected(Error::Invalid(std::format("no instance to bind the {} hook to", what)));

    auto hook = std::make_shared<KHook::Virtual<VtableObject, Ret, Args...>>(index, self, pre, post);
    hook->Add(static_cast<VtableObject*>(instance));
    return Detail::OwnHook(std::move(hook));
}

}  // namespace VoltMod
