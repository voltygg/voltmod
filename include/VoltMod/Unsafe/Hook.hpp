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

/** HookAction::Allow in KHook's vocabulary: run the original and keep its value. */
template <class Ret>
KHook::Return<Ret> Allow()
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
                return Allow<Ret>();
        }
    }
}

/** A post-handler is handed the value the call will return, and a void call has none to give.
 *  Written out rather than a conditional_t, which would form `void` as a parameter type. */
template <class Object, class Ret, class... Args>
struct PostHandler
{
    using Type = std::move_only_function<void(Object&, Args..., Ret)>;
};

template <class Object, class... Args>
struct PostHandler<Object, void, Args...>
{
    using Type = std::move_only_function<void(Object&, Args...)>;
};

/**
 * The handlers, and the two trampolines KHook dispatches into.
 *
 * Kept apart from `KHook::Virtual` because that is polymorphic and KHook extracts a raw code
 * address from the member pointers below: a virtual anywhere in this class would silently send
 * dispatch into garbage.
 */
template <class Object, class Ret, class... Args>
class HookCallbacks
{
public:
    using Pre = std::move_only_function<HookResult<Ret>(Object&, Args...)>;
    using Post = typename PostHandler<Object, Ret, Args...>::Type;

    HookCallbacks(Pre pre, Post post) : _pre(std::move(pre)), _post(std::move(post))
    {
        static_assert(!std::is_polymorphic_v<HookCallbacks>, "KHook dispatches through a raw member address");
    }

    KHook::Return<Ret> CallPre(Object* self, Args... args)
    {
        if (!_pre)
            return Allow<Ret>();
        return ToKHook(_pre(*self, std::forward<Args>(args)...));
    }

    KHook::Return<Ret> CallPost(Object* self, Args... args)
    {
        if (_post)
        {
            if constexpr (std::is_void_v<Ret>)
                _post(*self, std::forward<Args>(args)...);
            else
                // Safe even when a pre blocked the call: that path fills the override slot.
                _post(*self, std::forward<Args>(args)..., KHook::GetCurrentReturn<Ret>());
        }
        return Allow<Ret>();
    }

private:
    Pre _pre;
    Post _post;
};

/** A pre-handler that returns nothing means "let the call through". */
template <class Object, class Ret, class... Args, class Handler>
typename HookCallbacks<Object, Ret, Args...>::Pre MakePre(Handler&& handler)
{
    if constexpr (std::is_null_pointer_v<std::decay_t<Handler>>)
        return {};
    else if constexpr (std::is_void_v<std::invoke_result_t<std::decay_t<Handler>&, Object&, Args...>>)
        return [pre = std::forward<Handler>(handler)](Object& self, Args... args) mutable {
            pre(self, std::forward<Args>(args)...);
            return HookResult<Ret>{};
        };
    else
        return std::forward<Handler>(handler);
}

/** Everything one hook owns. Heap-pinned, because KHook keys its contexts by address. */
template <class Object, class Ret, class... Args>
struct HookState
{
    using Callbacks = HookCallbacks<Object, Ret, Args...>;

    KHook::Virtual<Object, Ret, Args...> Dispatch;
    Callbacks Handlers;

    /** Registers the context before any slot is configured, which is the order KHook needs. */
    HookState(typename Callbacks::Pre pre, typename Callbacks::Post post) : Handlers(std::move(pre), std::move(post))
    {
        Dispatch.AddContext(&Handlers, &Callbacks::CallPre, &Callbacks::CallPost);
    }

    ~HookState() { Dispatch.ClearHooks(); }

    HookState(const HookState&) = delete;
    HookState& operator=(const HookState&) = delete;
};

/** Dropping the Subscription destroys the hook, which removes it. */
template <class State>
Subscription AsSubscription(std::unique_ptr<State> hook)
{
    return Subscription([hook = std::move(hook)]() mutable { hook.reset(); });
}

template <class Pre, class Post>
constexpr void RequireAHandler()
{
    static_assert(!std::is_null_pointer_v<std::decay_t<Pre>> || !std::is_null_pointer_v<std::decay_t<Post>>,
                  "a hook with neither side would install nothing");
}

}  // namespace Detail

/**
 * @brief Hook one virtual method of an engine interface, for as long as the Subscription lives.
 *
 * KHook reads the vtable slot straight from @p method, so nothing has to be declared at namespace
 * scope. Only @p instance is hooked, not every object sharing its vtable. Pass `nullptr` for a
 * side you do not want, and return nothing from a pre-handler that only observes.
 *
 * @code
 * hooks.Add(VoltMod::HookInterface(&IServerGameDLL::GameFrame, gi.ServerGameDLL, nullptr,
 *                                  [this](IServerGameDLL&, bool, bool, bool) { Tick(); }));
 * @endcode
 */
template <class Iface, class Ret, class... Args, class Pre, class Post = std::nullptr_t>
[[nodiscard]] Subscription HookInterface(Ret (Iface::*method)(Args...), Iface* instance, Pre&& pre,
                                         Post&& post = nullptr)
{
    Detail::RequireAHandler<Pre, Post>();

    auto hook = std::make_unique<Detail::HookState<Iface, Ret, Args...>>(
        Detail::MakePre<Iface, Ret, Args...>(std::forward<Pre>(pre)), std::forward<Post>(post));
    hook->Dispatch.Configure(method);
    if (instance)
        hook->Dispatch.Add(instance);

    return Detail::AsSubscription(std::move(hook));
}

/**
 * @brief Hook a gamedata-bound slot on every object sharing a class vtable.
 *
 * @param what Names the hook in the log and in any error.
 * @param binding The vtable index and class table gamedata resolved.
 * @param sample A live instance, read only to warn about a stale gamedata class name. Pass nullptr
 *               when none exists yet.
 */
template <class Object, class Ret, class... Args, class Pre, class Post = std::nullptr_t>
[[nodiscard]] Result<Subscription> HookVTable(std::string_view what,
                                              const VHookBinding<Object, Ret(Args...)>& binding, Pre&& pre,
                                              Post&& post = nullptr, void* sample = nullptr)
{
    Detail::RequireAHandler<Pre, Post>();

    const int index = binding.Method.Index();
    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", what)));
    if (!binding.Table)
        return std::unexpected(Error::Engine(std::format("the {} class vtable did not bind", what)));

    // A live instance detects a stale gamedata class name without blocking early installation.
    if (sample && *static_cast<void**>(sample) != binding.Table.Table())
        Log::Warn("{}: a live instance's vtable differs from {}; wrong class name?", what, binding.Table.Class());

    auto hook = std::make_unique<Detail::HookState<Object, Ret, Args...>>(
        Detail::MakePre<Object, Ret, Args...>(std::forward<Pre>(pre)), std::forward<Post>(post));
    hook->Dispatch.Configure(index);

    // KHook reads the vtable out of the object it is given, and the table is all we have.
    void* asObject = binding.Table.Table();
    hook->Dispatch.AddGlobal(reinterpret_cast<Object*>(&asObject));

    Log::Info("{} hook installed on {} vtable (index {}).", what, binding.Table.Class(), index);
    return Detail::AsSubscription(std::move(hook));
}

/** The engine's own implementation of @p method, bypassing every hook on the slot. */
template <class Iface, class Ret, class... Args, class... Passed>
Ret CallOriginal(Ret (Iface::*method)(Args...), Iface* instance, Passed&&... args)
{
    return KHook::CallOriginal(method, instance, std::forward<Passed>(args)...);
}

}  // namespace VoltMod
