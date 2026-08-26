#pragma once

// The VoltMod public vocabulary in one include.
//
// Every public name lives directly in `VoltMod`, so this header only gathers the
// headers that declare them - there is nothing to hoist or alias. Include it
// wherever a plugin wants the whole surface; include the individual headers when
// a translation unit only needs a few.

#include <VoltMod/App/JsonConfig.hpp>
#include <VoltMod/App/MetamodPlugin.hpp>
#include <VoltMod/App/PluginSettings.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StandardLoad.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Commands/CommandSpec.hpp>
#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Random.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlidingWindowScore.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Throttle.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Core/Validation.hpp>
#include <VoltMod/Engine/Clock.hpp>
#include <VoltMod/Engine/ConVarLease.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Map.hpp>
#include <VoltMod/Engine/NetChannel.hpp>
#include <VoltMod/Engine/ServerCommand.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/HitGroup.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/PawnPredicates.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <VoltMod/Events/EventTypes.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Hooks/ClientCvars.hpp>
#include <VoltMod/Hooks/Damage.hpp>
#include <VoltMod/Hooks/GlowVision.hpp>
#include <VoltMod/Hooks/InputHistory.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Hooks/UserCmd.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Http/HttpResult.hpp>
#include <VoltMod/Menu/Flow.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuContext.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Menu/Options/ChoiceOption.hpp>
#include <VoltMod/Messaging/CenterHtml.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Messaging/Vote.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Players/TargetResolver.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Unsafe/HookMacros.hpp>

// The Database vocabulary lives in <VoltMod/Database/Api.hpp>, deliberately
// outside this umbrella: it drags <pqxx/pqxx> into every including TU, and
// most plugin code never touches the database.
