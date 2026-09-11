#include <khook.hpp>

int KHookHeaderCompiles()
{
    return KHook::INVALID_HOOK == static_cast<KHook::HookID_t>(-1) ? 1 : 0;
}
