#pragma once

#include <VoltMod/Core/Result.hpp>
#include <functional>
#include <string_view>

namespace VoltMod
{

/** What a VScript binding calls: a function, or the vtable index a virtual binding dispatches through. */
struct ScriptTarget
{
    void* Address = nullptr;
    int Index = -1;
};

/** Finds VScript binding @p name on the class with vtable @p classTable or on its bases. Injected
 *  so the resolver stays SDK-free. */
using ScriptBindingLookup = std::function<Result<ScriptTarget>(void* classTable, std::string_view name)>;

/** The @ref ScriptBindingLookup the host uses. */
Result<ScriptTarget> FindScriptBinding(void* classTable, std::string_view name);

}  // namespace VoltMod
