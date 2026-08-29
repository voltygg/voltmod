#include "MenuHostSeam.hpp"

#include "Menu/HostCalls.hpp"

#include <cstddef>
#include <utility>

namespace VoltModTests
{

MenuHostCalls& HostCalls()
{
    static MenuHostCalls calls;
    return calls;
}

VoltMod::MenuHost& NoMenuHost()
{
    // Storage, not a host: the two functions below are the only things a row does to a session,
    // and neither reads this. A pointer is all that travels through MenuItem::Activate.
    static std::max_align_t storage;
    return *reinterpret_cast<VoltMod::MenuHost*>(&storage);
}

}  // namespace VoltModTests

namespace VoltMod::Internal
{

void OpenMenu(MenuHost&, int, std::shared_ptr<Menu> menu)
{
    auto& calls = VoltModTests::HostCalls();
    ++calls.Opened;
    calls.LastMenu = std::move(menu);
}

void BeginInput(MenuHost&, int, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    auto& calls = VoltModTests::HostCalls();
    ++calls.Inputs;
    calls.LastPrompt = std::move(prompt);
    calls.LastInput = std::move(callback);
}

}  // namespace VoltMod::Internal
