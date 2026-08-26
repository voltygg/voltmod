#pragma once

// Umbrella of curated short names for the VoltMod public vocabulary.
//
// The library groups its API into module namespaces (Sdk, Menu, Players, ...),
// which is useful internally but forces three-segment call sites like
// `VoltMod::Sdk::PlayerController`. This header hoists the commonly-used public
// types and free functions to the top-level `VoltMod` namespace so consumers can
// write `VoltMod::PlayerController` instead - in headers and sources alike,
// without a namespace-scope using-directive (which would be unsafe in a header).
//
// The fully-qualified module names keep working; the short forms are synonyms.
// Include this header wherever the short `VoltMod::Type` spelling is used.

#include <VoltMod/App/JsonConfig.hpp>
#include <VoltMod/App/MetamodPlugin.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StandardLoad.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Commands/CommandSpec.hpp>
#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/HookMacros.hpp>
#include <VoltMod/Core/ILogger.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/PluginPolicy.hpp>
#include <VoltMod/Core/PluginSettings.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>
#include <VoltMod/Runtime.hpp>
// The Database vocabulary lives in <VoltMod/Database/Api.hpp>, deliberately
// outside this umbrella: it drags <pqxx/pqxx> into every including TU, and
// most plugin code never touches the database.
#include <VoltMod/Core/AngleMath.hpp>
#include <VoltMod/Core/DecayingScore.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Random.hpp>
#include <VoltMod/Core/SlidingWindowScore.hpp>
#include <VoltMod/Core/SlotThrottle.hpp>
#include <VoltMod/Core/StringUtils.hpp>
#include <VoltMod/Core/TimeUtils.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Core/Validation.hpp>
#include <VoltMod/Http/HttpResult.hpp>
#include <VoltMod/Menu/Flow.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuContext.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <VoltMod/Menu/Options/ChoiceOption.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Players/PerSlot.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/TargetResolver.hpp>
#include <VoltMod/Sdk/Client/ClientCvarService.hpp>
#include <VoltMod/Sdk/Engine/ConVarService.hpp>
#include <VoltMod/Sdk/Engine/MapService.hpp>
#include <VoltMod/Sdk/Engine/NetChannel.hpp>
#include <VoltMod/Sdk/Engine/ServerClock.hpp>
#include <VoltMod/Sdk/Engine/ServerCommand.hpp>
#include <VoltMod/Sdk/Entity/DamageHook.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/EntityKeyValues.hpp>
#include <VoltMod/Sdk/Entity/EntityOps.hpp>
#include <VoltMod/Sdk/Entity/HitGroup.hpp>
#include <VoltMod/Sdk/Entity/ItemService.hpp>
#include <VoltMod/Sdk/Entity/MoveType.hpp>
#include <VoltMod/Sdk/Entity/PawnOps.hpp>
#include <VoltMod/Sdk/Entity/PawnPredicates.hpp>
#include <VoltMod/Sdk/Entity/PawnService.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>
#include <VoltMod/Sdk/Events/GameEventService.hpp>
#include <VoltMod/Sdk/Events/GameEvents.hpp>
#include <VoltMod/Sdk/Messaging/ChatInputCapture.hpp>
#include <VoltMod/Sdk/Messaging/PanoramaVote.hpp>
#include <VoltMod/Sdk/Messaging/PersistentCenterHtml.hpp>
#include <VoltMod/Sdk/Messaging/UserMessage.hpp>
#include <VoltMod/Sdk/Movement/InputHistoryService.hpp>
#include <VoltMod/Sdk/Movement/MovementHook.hpp>
#include <VoltMod/Sdk/Movement/TeleportTracker.hpp>
#include <VoltMod/Sdk/Movement/UserCmd.hpp>
#include <VoltMod/Sdk/Visibility/GlowVision.hpp>
#include <VoltMod/Sdk/Visibility/VisibilityService.hpp>

namespace VoltMod
{

// Core
using App::JsonConfig;
using App::LoadStandardConfig;
using App::MetamodPlugin;
using App::PluginInfo;
using App::ServiceExchange;
using App::StandardLoadOptions;
using App::StatusService;
using Core::AddonDir;
using Core::AddonFile;
using Core::CallbackRegistry;
using Core::EffectManager;
using Core::EffectSpec;
using Core::ILogger;
using Core::IsValidSlot;
using Core::LoadReport;
using Core::MaxPlayers;
using Core::PluginPolicy;
using Core::RandomIndex;
using Core::ResolvePath;
using Core::Scheduler;
using Core::StageResult;
using Core::StageStatus;
using Core::StandardPluginSettings;
using Core::Subscription;
using Players::EffectChoice;
using Players::EffectDescriptor;
using Players::EffectInstance;
using Players::EffectScope;
using Players::ParamEffectDescriptor;

// Sdk
using Sdk::ChatInputCapture;
using Sdk::ClientCvarService;
using Sdk::ClientCvarStatus;
using Sdk::ConVarService;
using Sdk::DamageHook;
using Sdk::DamageView;
using Sdk::EntityKeyValues;
using Sdk::EntityOpsService;
using Sdk::EntitySystem;
using Sdk::GameEventService;
using Sdk::GlowVision;
using Sdk::HasPawnFlag;
using Sdk::HitGroup;
using Sdk::InMoveType;
using Sdk::InputHistorySample;
using Sdk::InputHistoryService;
using Sdk::ItemService;
using Sdk::MapService;
using Sdk::MessageKind;
using Sdk::MessageSystem;
using Sdk::MovementHook;
using Sdk::MoveType;
using Sdk::NetChannelService;
using Sdk::PanoramaVote;
using Sdk::PawnService;
using Sdk::PersistentCenterHtml;
using Sdk::PlayerController;
using Sdk::RawConVar;
using Sdk::ServerClock;
using Sdk::ServerCommand;
using Sdk::SubtickMove;
using Sdk::TeleportTracker;
using Sdk::UserCmdView;
using Sdk::VisibilityService;
using Sdk::VoteEndReason;
using Sdk::VoteTally;
namespace PawnOps = Sdk::PawnOps;
namespace Events = Sdk::Events;

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
using Players::PerSlot;
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
using Core::DecayingScore;
using Core::PairThrottle;
using Core::ParseDuration;
using Core::SlidingWindowScore;
using Core::SlotThrottle;
using Core::StringUtils;
using Core::Throttle;
using Core::TimeUtils;
using Core::Tokens;
using Core::Translations;
namespace AngleMath = Core::AngleMath;
namespace Log = Core::Log;
namespace Validation = Core::Validation;

}  // namespace VoltMod
