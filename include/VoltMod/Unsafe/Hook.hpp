#pragma once

#include <VoltMod/Core/HookResult.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <format>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace VoltMod
{
namespace Detail
{

/** KHook's way of saying: run the original and keep its value. */
template <class Ret>
KHook::Return<Ret> RunOriginal()
{
    if constexpr (std::is_void_v<Ret>)
        return {KHook::Action::Ignore};
    else
        // Never read, but KHook still wants the slot filled.
        return {KHook::Action::Ignore, Ret{}};
}

template <class Ret>
KHook::Return<Ret> ToKHook(HookResult<Ret> result)
{
    if constexpr (std::is_void_v<Ret>)
    {
        return {result.Action() == HookAction::Block ? KHook::Action::Supersede : KHook::Action::Ignore};
    }
    else
    {
        switch (result.Action())
        {
            case HookAction::Replace:
                return {KHook::Action::Override, std::move(result).Value()};
            case HookAction::Block:
                return {KHook::Action::Supersede, std::move(result).Value()};
            default:
                return RunOriginal<Ret>();
        }
    }
}

/** An after-handler also gets the return value; a void call has none. Not a conditional_t, which
 *  would form a `void` parameter. */
template <class Object, class Ret, class... Args>
struct AfterHandler
{
    using Type = std::move_only_function<void(Object&, Args..., Ret)>;
};

template <class Object, class... Args>
struct AfterHandler<Object, void, Args...>
{
    using Type = std::move_only_function<void(Object&, Args...)>;
};

/** The handlers and the trampolines KHook calls through raw member addresses, so never polymorphic. */
template <class Object, class Ret, class... Args>
class HookHandlers
{
public:
    using Before = std::move_only_function<HookResult<Ret>(Object&, Args...)>;
    using After = typename AfterHandler<Object, Ret, Args...>::Type;

    HookHandlers(Before before, After after) : _before(std::move(before)), _after(std::move(after))
    {
        static_assert(!std::is_polymorphic_v<HookHandlers>, "KHook dispatches through a raw member address");
    }

    KHook::Return<Ret> RunBefore(Object* self, Args... args)
    {
        if (!_before)
            return RunOriginal<Ret>();
        return ToKHook(_before(*self, std::forward<Args>(args)...));
    }

    KHook::Return<Ret> RunAfter(Object* self, Args... args)
    {
        if (_after)
        {
            if constexpr (std::is_void_v<Ret>)
                _after(*self, std::forward<Args>(args)...);
            else
                // Safe even when the before-handler blocked the call: that path fills the override slot.
                _after(*self, std::forward<Args>(args)..., KHook::GetCurrentReturn<Ret>());
        }
        return RunOriginal<Ret>();
    }

private:
    Before _before;
    After _after;
};

/** A before-handler that returns nothing lets the call through. */
template <class Object, class Ret, class... Args, class Handler>
typename HookHandlers<Object, Ret, Args...>::Before MakeBefore(Handler&& handler)
{
    if constexpr (std::is_null_pointer_v<std::decay_t<Handler>>)
        return {};
    else if constexpr (std::is_void_v<std::invoke_result_t<std::decay_t<Handler>&, Object&, Args...>>)
        return [before = std::forward<Handler>(handler)](Object& self, Args... args) mutable {
            before(self, std::forward<Args>(args)...);
            return HookResult<Ret>{};
        };
    else
        return std::forward<Handler>(handler);
}

/** A KHook hook and its handlers. Heap-allocated: KHook keys contexts by address. */
template <class KHookType, class Object, class Ret, class... Args>
struct InstalledHook
{
    using HandlerSet = HookHandlers<Object, Ret, Args...>;

    HandlerSet Handlers;
    KHookType Hook;  // declared last, so it is removed before the handlers are destroyed

    /** KHook needs the context before the hook is configured. */
    InstalledHook(typename HandlerSet::Before before, typename HandlerSet::After after)
        : Handlers(std::move(before), std::move(after))
    {
        Hook.AddContext(&Handlers, &HandlerSet::RunBefore, &HandlerSet::RunAfter);
    }

    InstalledHook(const InstalledHook&) = delete;
    InstalledHook& operator=(const InstalledHook&) = delete;
};

/** Dropping the Subscription destroys the hook, which removes it. */
template <class Hook>
Subscription ToSubscription(std::unique_ptr<Hook> hook)
{
    return Subscription([hook = std::move(hook)]() mutable { hook.reset(); });
}

template <class Before, class After>
constexpr void RequireHandler()
{
    static_assert(!std::is_null_pointer_v<std::decay_t<Before>> || !std::is_null_pointer_v<std::decay_t<After>>,
                  "a hook with neither handler would install nothing");
}

}  // namespace Detail

/**
 * @brief Hook one virtual method of an engine interface, for as long as the Subscription lives.
 *
 * KHook reads the vtable slot straight from @p method. Only @p instance is hooked, not every object
 * sharing its vtable. Pass `nullptr` for a handler you do not want; a before-handler that only
 * observes returns nothing.
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
    Detail::RequireHandler<Before, After>();

    auto hook = std::make_unique<Detail::InstalledHook<KHook::Virtual<Iface, Ret, Args...>, Iface, Ret, Args...>>(
        Detail::MakeBefore<Iface, Ret, Args...>(std::forward<Before>(before)), std::forward<After>(after));
    hook->Hook.Configure(method);
    if (instance)
        hook->Hook.Add(instance);

    return Detail::ToSubscription(std::move(hook));
}

/**
 * @brief Hook a gamedata-bound slot on every object sharing a class vtable.
 *
 * Catches only calls through that table, even when the slot's code is shared with other classes.
 *
 * @param name Names the hook in the log and in any error.
 * @param slot The vtable index and class table gamedata resolved.
 * @param liveInstance Read only to warn about a stale gamedata class name; nullptr when none exists yet.
 */
template <class Object, class Ret, class... Args, class Before, class After = std::nullptr_t>
[[nodiscard]] Result<Subscription> HookClassSlot(std::string_view name, const ClassSlot<Object, Ret(Args...)>& slot,
                                                 Before&& before, After&& after = nullptr,
                                                 void* liveInstance = nullptr)
{
    Detail::RequireHandler<Before, After>();

    const int index = slot.Function.Index();
    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", name)));
    if (!slot.Table)
        return std::unexpected(Error::Engine(std::format("the {} class vtable did not bind", name)));

    if (liveInstance && *static_cast<void**>(liveInstance) != slot.Table.Ptr())
        Log::Warn("{}: a live instance's vtable differs from {}; wrong class name?", name, slot.Table.ClassName());

    auto hook = std::make_unique<Detail::InstalledHook<KHook::Virtual<Object, Ret, Args...>, Object, Ret, Args...>>(
        Detail::MakeBefore<Object, Ret, Args...>(std::forward<Before>(before)), std::forward<After>(after));
    hook->Hook.Configure(index);

    // KHook reads the vtable out of the object it is given, and the table is all we have.
    void* asObject = slot.Table.Ptr();
    hook->Hook.AddGlobal(reinterpret_cast<Object*>(&asObject));

    Log::Info("{} hook installed on {} vtable (index {}).", name, slot.Table.ClassName(), index);
    return Detail::ToSubscription(std::move(hook));
}

/**
 * @brief Hook a signature-bound function at its entry, catching every caller.
 *
 * For functions no vtable slot reaches: non-virtual, or on a secondary base. Never use it on a
 * slot's code, which other classes may share.
 *
 * @param name Names the hook in the log and in any error.
 * @param function The binding; its first parameter is the object the function runs on.
 */
template <class Object, class Ret, class... Args, class Before, class After = std::nullptr_t>
[[nodiscard]] Result<Subscription> HookFunction(std::string_view name, const Fn<Ret(Object*, Args...)>& function,
                                                Before&& before, After&& after = nullptr)
{
    Detail::RequireHandler<Before, After>();

    if (!function)
        return std::unexpected(Error::Unsupported(std::format("the {} signature did not bind", name)));

    auto hook = std::make_unique<Detail::InstalledHook<KHook::Member<Object, Ret, Args...>, Object, Ret, Args...>>(
        Detail::MakeBefore<Object, Ret, Args...>(std::forward<Before>(before)), std::forward<After>(after));
    hook->Hook.Configure(static_cast<const void*>(function.Ptr()));

    Log::Info("{} hook installed.", name);
    return Detail::ToSubscription(std::move(hook));
}

/** The engine's own implementation of @p method, bypassing every hook on the slot. */
template <class Iface, class Ret, class... Args, class... Passed>
Ret CallOriginal(Ret (Iface::*method)(Args...), Iface* instance, Passed&&... args)
{
    return KHook::CallOriginal(method, instance, std::forward<Passed>(args)...);
}

}  // namespace VoltMod
