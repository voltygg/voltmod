#pragma once

// The VoltMod core vocabulary, the Runtime facade, and player/command/plugin plumbing in
// one include - everything a plugin's OnLoad and command handlers touch without opting
// into a specific tier.
//
// Every public name lives directly in `VoltMod`, so this header only gathers the headers
// that declare them - there is nothing to hoist or alias. It deliberately does not include
// the Menu-building surface (`<VoltMod/Menu/Api.hpp>`) or anything that reaches nlohmann
// (`<VoltMod/App/Config.hpp>` for a JsonConfig-backed settings struct): most translation
// units need none of those, and each is one explicit include away.
//
// The Unsafe tier is a naming convention here, not an include boundary: Runtime holds
// UnsafeServices and HookServices by value, so all of `<VoltMod/Unsafe/Api.hpp>` except
// HookMacros.hpp is already in every translation unit that includes this header. Include
// `<VoltMod/Unsafe/Api.hpp>` anyway where you mean to use it - it says so at the call site,
// and it is the one that carries VOLTMOD_VHOOK*. Entities and Hooks types stay reachable through Runtime
// (it holds one of each service by value) even without their own `Entities/Api.hpp` or
// `Hooks/Api.hpp`; include those two directly for the rest of the module - the frame-local
// wrappers and free functions Runtime itself has no member of.

#include <VoltMod/App/MetamodPlugin.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StandardLoad.hpp>
#include <VoltMod/Commands/Args.hpp>
#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SteamId.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Throttle.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Runtime.hpp>

// <VoltMod/Entities/Api.hpp>, <VoltMod/Hooks/Api.hpp>, <VoltMod/Menu/Api.hpp> and
// <VoltMod/Unsafe/Api.hpp> gather those modules' full public surfaces; a plugin opts into
// each by including it explicitly.
//
// <VoltMod/App/Config.hpp> gathers JsonConfig, PluginSettings and Json - a plugin's own
// Config.hpp includes it explicitly instead of pulling nlohmann into every translation
// unit that happens to include this umbrella.
//
// <VoltMod/Database/Api.hpp> gathers the Database vocabulary, also outside this umbrella:
// it drags <pqxx/pqxx> into every including TU, and most plugin code never touches the
// database.
