#pragma once

// Umbrella of curated short names for the VoltMod public vocabulary.
//
// The library groups its API into module namespaces (Engine, Entities, Hooks,
// Events, Messaging, Players, Menu, ...), which is useful internally but forces
// three-segment call sites like `VoltMod::Entities::PlayerController`. This header
// hoists the commonly-used public types and free functions to the top-level
// `VoltMod` namespace so consumers can write `VoltMod::PlayerController` instead -
// in headers and sources alike, without a namespace-scope using-directive (which
// would be unsafe in a header).
//
// The fully-qualified module names keep working; the short forms are synonyms.
// Include this header wherever the short `VoltMod::Type` spelling is used.

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
#include <VoltMod/Core/Policy.hpp>
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
#include <VoltMod/Players/TargetResolver.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Unsafe/HookMacros.hpp>
// The Database vocabulary lives in <VoltMod/Database/Api.hpp>, deliberately
// outside this umbrella: it drags <pqxx/pqxx> into every including TU, and
// most plugin code never touches the database.

namespace VoltMod
{

// Core
using App::JsonConfig;
using App::LoadStandardConfig;
using App::MetamodPlugin;
using App::PluginInfo;
using App::ServiceExchange;
using App::StandardLoadOptions;
using App::StandardPluginSettings;
using App::StatusService;
using Core::AddonDir;
using Core::AddonFile;
using Core::CallbackRegistry;
using Core::EffectManager;
using Core::EffectSpec;
using Core::IsValidSlot;
using Core::LoadReport;
using Core::MaxPlayers;
using Core::PerSlot;
using Core::Policy;
using Core::RandomIndex;
using Core::ResolvePath;
using Core::Scheduler;
using Core::StageResult;
using Core::StageStatus;
using Core::Subscription;
using Players::EffectChoice;
using Players::EffectDescriptor;
using Players::EffectInstance;
using Players::EffectScope;
using Players::ParamEffectDescriptor;

// Engine
using Engine::Clock;
using Engine::ConVarLease;
using Engine::ConVars;
using Engine::ConVarStorage;
using Engine::Map;
using Engine::NetChannels;
using Engine::ServerCommand;

// Entities
using Entities::EntityOps;
using Entities::EntitySystem;
using Entities::HasPawnFlag;
using Entities::HitGroup;
using Entities::InMoveType;
using Entities::Items;
using Entities::KeyValues;
using Entities::MoveType;
using Entities::Pawns;
using Entities::PlayerController;
namespace PawnOps = Entities::PawnOps;

// Hooks
using Hooks::ChatInput;
using Hooks::ClientCvars;
using Hooks::ClientCvarStatus;
using Hooks::Damage;
using Hooks::DamageView;
using Hooks::GlowVision;
using Hooks::InputHistory;
using Hooks::InputHistorySample;
using Hooks::Movement;
using Hooks::SubtickMove;
using Hooks::Teleport;
using Hooks::UserCmdView;
using Hooks::Visibility;

// Events (the typed event structs stay under VoltMod::Events, e.g. Events::PlayerDeath)
using Events::GameEvents;

// Messaging
using Messaging::CenterHtml;
using Messaging::MessageKind;
using Messaging::Messages;
using Messaging::Vote;
using Messaging::VoteEndReason;
using Messaging::VoteTally;

// Menu  (the built-menu data model is Menu::MenuView; `Menu` is the namespace)
using Menu::AppendPlayerRows;
using Menu::BuildConfirmDialog;
using Menu::BuildDurationPicker;
using Menu::BuildPaletteChoices;
using Menu::BuildPlayerPicker;
using Menu::ChoiceOption;
using Menu::ConfirmDialogSpec;
using Menu::Flow;
using Menu::MenuBuilder;
using Menu::MenuContext;
using Menu::MenuManager;
using Menu::MenuOption;
using Menu::MenuView;

// Players
using Players::Action;
using Players::ActionContext;
using Players::ActionDispatcher;
using Players::CanTargetFn;
using Players::EffectDispatcher;
using Players::ParamAction;
using Players::Player;
using Players::PlayerManager;
using Players::ResolveTargets;
using Players::TargetError;
using Players::TargetFailure;
using Players::TargetRules;

// Commands (arg factories keep their terse names: VoltMod::Target(), VoltMod::Duration(), ...)
using Commands::ArgKind;
using Commands::ArgSpec;
using Commands::CommandContext;
using Commands::CommandManager;
using Commands::CommandResult;
using Commands::CommandSpec;
using Commands::Duration;
using Commands::Int;
using Commands::ReasonTail;
using Commands::SteamId64;
using Commands::Target;
using Commands::TargetOrSteamId;
using Commands::Word;

// Http
using Http::HttpClient;
using Http::HttpResult;

// Utils
using Core::PairThrottle;
using Core::ParseDuration;
using Core::SlidingWindowScore;
using Core::Strings;
using Core::Throttle;
using Core::Time;
using Core::Tokens;
using Core::Translations;
namespace Log = Core::Log;
namespace Validation = Core::Validation;

}  // namespace VoltMod
