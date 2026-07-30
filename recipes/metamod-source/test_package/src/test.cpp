// Compiling SourceHook proves the package's include layout matches the
// submodule layout CS2KitSdk.cmake expects (core/ + core/sourcehook/).
#include <sourcehook.h>

int SourceHookHeaderCompiles()
{
    SourceHook::ISourceHook* hook = nullptr;
    return hook == nullptr ? 1 : 0;
}
