#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace VoltMod
{

/** What a pre-handler decides should happen to the call it intercepted. */
enum class HookAction : uint8_t
{
    Allow = 0,  ///< Let the engine's own handler run and keep its return value.
    Replace,    ///< Let it run, but return this value instead of its own.
    Block,      ///< Skip it entirely and return this value.
};

/**
 * @brief A hook pre-handler's verdict.
 *
 * Default-constructs to @ref HookAction::Allow, so `return {};` passes the call through. A handler
 * that returns nothing at all is treated the same way.
 *
 * Names no hooking machinery, so a header may return one without reaching
 * `<VoltMod/Unsafe/Hook.hpp>`.
 */
template <class Ret>
class HookResult
{
    // A passing handler supplies no value, so one has to be synthesised for the engine.
    static_assert(std::is_default_constructible_v<Ret>, "a hooked return type must default-construct");

public:
    HookResult() = default;

    /** Run the original, then return @p value rather than what it produced. */
    [[nodiscard]] static HookResult Replace(Ret value) { return {HookAction::Replace, std::move(value)}; }

    /** Do not run the original at all; return @p value instead. */
    [[nodiscard]] static HookResult Block(Ret value) { return {HookAction::Block, std::move(value)}; }

    [[nodiscard]] HookAction Action() const noexcept { return _action; }

    /** Meaningless unless @ref Action is Replace or Block. */
    [[nodiscard]] const Ret& Value() const& noexcept { return _value; }
    [[nodiscard]] Ret Value() && { return std::move(_value); }

private:
    HookResult(HookAction action, Ret value) : _action(action), _value(std::move(value)) {}

    HookAction _action = HookAction::Allow;
    Ret _value{};
};

/** Nothing to substitute, so there is no Replace: a void call either runs or it does not. */
template <>
class HookResult<void>
{
public:
    HookResult() = default;

    /** Skip the engine's own handler. */
    [[nodiscard]] static HookResult Block() { return HookResult(HookAction::Block); }

    [[nodiscard]] HookAction Action() const noexcept { return _action; }

private:
    explicit HookResult(HookAction action) noexcept : _action(action) {}

    HookAction _action = HookAction::Allow;
};

}  // namespace VoltMod
