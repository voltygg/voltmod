#pragma once

// Core vocabulary, Runtime, and common plugin plumbing. Include each module's Api.hpp when
// using its wider surface. JSON-backed configuration belongs in <VoltMod/App/Config.hpp>,
// which keeps Glaze out of translation units that only need this umbrella.

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
