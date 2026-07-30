// Proves the include layout (core/ + core/sourcehook/) matches what CS2KitSdk.cmake expects.
#include <sourcehook.h>

int SourceHookHeaderCompiles()
{
    SourceHook::ISourceHook* hook = nullptr;
    return hook == nullptr ? 1 : 0;
}
