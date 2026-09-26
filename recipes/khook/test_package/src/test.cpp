#include <khook.hpp>

int main()
{
    // Calls into the static implementation, then stops its worker threads.
    const bool linked = KHook::FindOriginal(nullptr) == nullptr;
    KHook::Shutdown();
    return linked ? 0 : 1;
}
