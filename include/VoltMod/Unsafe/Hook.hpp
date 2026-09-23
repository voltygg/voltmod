#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/HookResult.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Engine/GameData/BindingTypes.hpp>
#include <format>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace VoltMod
{
namespace Internal
{

inline KHook::Action ToKHookAction(HookAction action)
{
    switch (action)
    {
    case HookAction::Replace:
        return KHook::Action::Override;
    case HookAction::Block:
        return KHook::Action::Supersede;
    default:
        return KHook::Action::Ignore;
    }
}

/** KHook ignores the default value carried by an Allow result. */
template <class Ret>
KHook::Return<Ret> ToKHook(HookResult<Ret> result)
{
    return {ToKHookAction(result.Action()), std::move(result).Value()};
}

inline KHook::Return<void> ToKHook(HookResult<void> result)
{
    return {ToKHookAction(result.Action())};
}

/**
 * KHook storage and the before/after handlers it invokes.
 *
 * The object is heap allocated and non-polymorphic because KHook stores its address and invokes
 * RunBefore and RunAfter through raw member addresses.
 *
 * @tparam HookKind KHook::Virtual or KHook::Member.
 */
template <template <class, class, class...> class HookKind, class Object, class Ret, class... Args>
class InstalledHook
{
public:
    /** Either handler may be `nullptr`, but not both. */
    template <class BeforeHandler, class AfterHandler>
    InstalledHook(BeforeHandler&& before, AfterHandler&& after)
        : _before(WrapBefore(std::forward<BeforeHandler>(before))), _after(std::forward<AfterHandler>(after))
    {
        static_assert(!std::is_polymorphic_v<InstalledHook>, "KHook dispatches through a raw member address");
        static_assert(
            !std::is_null_pointer_v<std::decay_t<BeforeHandler>> || !std::is_null_pointer_v<std::decay_t<AfterHandler>>,
            "a hook with neither handler would install nothing");

        // KHook requires the context before Configure is called.
        Hook.AddContext(this, &InstalledHook::RunBefore, &InstalledHook::RunAfter);
    }

    InstalledHook(const InstalledHook&) = delete;
    InstalledHook& operator=(const InstalledHook&) = delete;

private:
    using Before = std::move_only_function<HookResult<Ret>(Object&, Args...)>;
    using After = std::move_only_function<void(Object&, Args...)>;

    /** Adapt a void before-handler to an Allow result. */
    template <class Handler>
    static Before WrapBefore(Handler&& handler)
    {
        if constexpr (std::is_null_pointer_v<std::decay_t<Handler>>)
        {
            return {};
        }
        else if constexpr (std::is_void_v<std::invoke_result_t<std::decay_t<Handler>&, Object&, Args...>>)
        {
            return [handler = std::forward<Handler>(handler)](Object& self, Args... args) mutable {
                handler(self, std::forward<Args>(args)...);
                return HookResult<Ret>{};
            };
        }
        else
        {
            return std::forward<Handler>(handler);
        }
    }

    KHook::Return<Ret> RunBefore(Object* self, Args... args)
    {
        return ToKHook(_before ? _before(*self, std::forward<Args>(args)...) : HookResult<Ret>{});
    }

    KHook::Return<Ret> RunAfter(Object* self, Args... args)
    {
        if (_after)
        {
            _after(*self, std::forward<Args>(args)...);
        }
        return ToKHook(HookResult<Ret>{});
    }

    Before _before;
    After _after;

public:
    HookKind<Object, Ret, Args...> Hook;  // Destroyed before the handlers it invokes.
};

/** Return a subscription that removes the hook when destroyed. */
template <class Hook>
Subscription ToSubscription(std::unique_ptr<Hook> hook)
{
    return Subscription([hook = std::move(hook)]() mutable { hook.reset(); });
}

}  // namespace Internal

/**
 * @brief Hook one virtual method of an engine interface for a subscription's lifetime.
 *
 * Only @p instance is hooked, even when other objects share its vtable. Pass `nullptr` for an
 * unused handler. A before-handler that only observes returns nothing.
 *
 * @code
 * hooks.Add(VoltMod::HookInterface(&IServerGameDLL::GameFrame, gi.ServerGameDLL, nullptr,
 *                                  [this](IServerGameDLL&, bool, bool, bool) { Tick(); }));
 * @endcode
 */
template <class Iface, class Ret, class... Args, class Before, class After = std::nullptr_t>
[[nodiscard]] Subscription HookInterface(Ret (Iface::*method)(Args...), Iface* instance, Before&& before,
                                         After&& after = nullptr)
{
    using Installed = Internal::InstalledHook<KHook::Virtual, Iface, Ret, Args...>;
    auto hook = std::make_unique<Installed>(std::forward<Before>(before), std::forward<After>(after));
    hook->Hook.Configure(method);
    if (instance)
    {
        hook->Hook.Add(instance);
    }

    return Internal::ToSubscription(std::move(hook));
}

/**
 * @brief Hook a gamedata-bound virtual function on objects sharing its class vtable.
 *
 * The hook catches calls through that table only, even when its slot code is shared by other
 * classes.
 *
 * @param name Names the hook in the log and in any error.
 * @param function Gamedata-resolved slot and class table. Its first parameter is the object.
 * @param before Handler invoked before the engine call, or nullptr.
 * @param after Handler invoked after the engine call, or nullptr.
 */
template <class Object, class Ret, class... Args, class Before, class After = std::nullptr_t>
[[nodiscard]] Result<Subscription> HookVirtual(std::string_view name, const VirtualFn<Ret(Object*, Args...)>& function,
                                               Before&& before, After&& after = nullptr)
{
    if (!function)
    {
        return std::unexpected(Error::Unsupported(std::format("the {} vtable slot did not bind", name)));
    }

    using Installed = Internal::InstalledHook<KHook::Virtual, Object, Ret, Args...>;
    auto hook = std::make_unique<Installed>(std::forward<Before>(before), std::forward<After>(after));
    hook->Hook.Configure(function.Index());

    // The class table is the only object KHook can use for a global hook.
    void* asObject = function.Table();
    hook->Hook.AddGlobal(reinterpret_cast<Object*>(&asObject));

    Log::Info("{} hook installed (vtable index {}).", name, function.Index());
    return Internal::ToSubscription(std::move(hook));
}

/**
 * @brief Hook a signature-bound function at its entry.
 *
 * Use this for functions without a vtable slot. The hook affects every caller of the matched
 * code, including callers from other classes that share it.
 *
 * @param name Names the hook in the log and in any error.
 * @param function The binding; its first parameter is the object the function runs on.
 * @param before Handler invoked before the engine call, or nullptr.
 * @param after Handler invoked after the engine call, or nullptr.
 */
template <class Object, class Ret, class... Args, class Before, class After = std::nullptr_t>
[[nodiscard]] Result<Subscription> HookFunction(std::string_view name, const Fn<Ret(Object*, Args...)>& function,
                                                Before&& before, After&& after = nullptr)
{
    if (!function)
    {
        return std::unexpected(Error::Unsupported(std::format("the {} signature did not bind", name)));
    }

    using Installed = Internal::InstalledHook<KHook::Member, Object, Ret, Args...>;
    auto hook = std::make_unique<Installed>(std::forward<Before>(before), std::forward<After>(after));
    hook->Hook.Configure(static_cast<const void*>(function.Ptr()));

    Log::Info("{} hook installed.", name);
    return Internal::ToSubscription(std::move(hook));
}

/** The engine's own implementation of @p method, bypassing every hook on the slot. */
template <class Iface, class Ret, class... Args, class... Passed>
Ret CallOriginal(Ret (Iface::*method)(Args...), Iface* instance, Passed&&... args)
{
    return KHook::CallOriginal(method, instance, std::forward<Passed>(args)...);
}

}  // namespace VoltMod
