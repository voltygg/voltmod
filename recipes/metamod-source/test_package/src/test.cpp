#include <sourcehook.h>

int SourceHookHeaderCompiles()
{
    SourceHook::ISourceHook* hook = nullptr;
    return hook == nullptr ? 1 : 0;
}
