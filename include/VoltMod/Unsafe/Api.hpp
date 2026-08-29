#pragma once

// Raw interfaces, gamedata and memory access, and vtable-hook support. Include explicitly
// in code that needs unsafe APIs.

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <VoltMod/Unsafe/HookMacros.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
