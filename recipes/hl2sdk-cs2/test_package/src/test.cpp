// Proves the package's include layout and defines match submodule-mode CS2KitSdk.cmake.
#include <mathlib/vector.h>
#include <tier1/utlvector.h>

float Hl2SdkHeadersCompile()
{
    Vector v(1.0f, 2.0f, 3.0f);
    CUtlVector<int> values;
    values.AddToTail(static_cast<int>(v.Length()));
    return v.Length() + static_cast<float>(values.Count());
}
