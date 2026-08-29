#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltModTests
{

/**
 * @brief What the two row kinds that reach a menu session did, recorded instead of done.
 *
 * `MenuHost` needs the HL2SDK, so it cannot be built in this SDK-free suite. Everything else
 * about a row is plain values, and the builder keeps its two host calls behind
 * `src/Menu/HostCalls.hpp` for exactly this reason: `MenuHostSeam.cpp` defines that pair here,
 * so an input row's validation and a submenu row's factory are testable without a live session.
 */
struct MenuHostCalls
{
    int Opened = 0;
    std::shared_ptr<VoltMod::Menu> LastMenu;
    int Inputs = 0;
    std::string LastPrompt;
    /** The callback the input row handed the session; call it with a chat line. */
    std::function<bool(int, std::string_view)> LastInput;

    void Reset() { *this = {}; }
};

/** The recorder the seam writes to. One per test binary, so reset it at the top of a case. */
MenuHostCalls& HostCalls();

/**
 * A `MenuHost&` for `MenuItem::Activate`, which takes one by reference. Nothing dereferences it:
 * the seam above records the call and drops the reference.
 */
VoltMod::MenuHost& NoMenuHost();

}  // namespace VoltModTests
