#pragma once

// Raw interfaces, gamedata and memory access, and vtable-hook support. Include explicitly
// in code that needs unsafe APIs.

#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Memory/MemoryAccess.hpp>
#include <VoltMod/Engine/Net/RecipientFilter.hpp>
#include <VoltMod/Unsafe/HookMacros.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
