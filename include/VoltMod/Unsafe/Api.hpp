#pragma once

// The Unsafe tier's public surface in one include: raw interface and gamedata access, memory
// pokes, and the vtable-hook machinery built on them. Opt in explicitly - most plugin code
// never needs this - by including this header (or the individual ones) where it does.

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <VoltMod/Unsafe/HookMacros.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
