#pragma once

// Umbrella of curated short names for the CS2Kit public vocabulary.
//
// The library groups its API into module namespaces (Sdk, Menu, Players, ...),
// which is useful internally but forces three-segment call sites like
// `CS2Kit::Sdk::PlayerController`. This header hoists the commonly-used public
// types and free functions to the top-level `CS2Kit` namespace so consumers can
// write `CS2Kit::PlayerController` instead - in headers and sources alike,
// without a namespace-scope using-directive (which would be unsafe in a header).
//
// The fully-qualified module names keep working; the short forms are synonyms.
// Include this header wherever the short `CS2Kit::Type` spelling is used.

#include <CS2Kit/App/JsonConfig.hpp>
#include <CS2Kit/App/MetamodPlugin.hpp>
#include <CS2Kit/App/ServiceExchange.hpp>
#include <CS2Kit/App/StandardLoad.hpp>
#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Commands/CommandSpec.hpp>
#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <CS2Kit/Core/EffectManager.hpp>
#include <CS2Kit/Core/HookMacros.hpp>
#include <CS2Kit/Core/ILogger.hpp>
#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/Paths.hpp>
#include <CS2Kit/Core/PluginPolicy.hpp>
#include <CS2Kit/Core/PluginSettings.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Core/Subscription.hpp>
#include <CS2Kit/Players/EffectDescriptor.hpp>
#include <CS2Kit/Runtime.hpp>
// The Database vocabulary lives in <CS2Kit/Database/Api.hpp>, deliberately
// outside this umbrella: it drags <pqxx/pqxx> into every including TU, and
// most plugin code never touches the database.
#include <CS2Kit/Http/HttpResult.hpp>
#include <CS2Kit/Menu/Flow.hpp>
#include <CS2Kit/Menu/Menu.hpp>
#include <CS2Kit/Menu/MenuBuilder.hpp>
#include <CS2Kit/Menu/MenuContext.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Menu/MenuOption.hpp>
#include <CS2Kit/Menu/MenuPresets.hpp>
#include <CS2Kit/Menu/Options/ChoiceOption.hpp>
#include <CS2Kit/Players/ActionDispatcher.hpp>
#include <CS2Kit/Players/PerSlot.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Players/TargetResolver.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <CS2Kit/Sdk/ClientCvarService.hpp>
#include <CS2Kit/Sdk/ConVarService.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/EntityKeyValues.hpp>
#include <CS2Kit/Sdk/EntityOps.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameEvents.hpp>
#include <CS2Kit/Sdk/GlowVision.hpp>
#include <CS2Kit/Sdk/InputHistoryService.hpp>
#include <CS2Kit/Sdk/MoveType.hpp>
#include <CS2Kit/Sdk/MovementHook.hpp>
#include <CS2Kit/Sdk/NetChannel.hpp>
#include <CS2Kit/Sdk/PawnOps.hpp>
#include <CS2Kit/Sdk/PawnPredicates.hpp>
#include <CS2Kit/Sdk/PersistentCenterHtml.hpp>
#include <CS2Kit/Sdk/PlayerController.hpp>
#include <CS2Kit/Sdk/ServerClock.hpp>
#include <CS2Kit/Sdk/ServerCommand.hpp>
#include <CS2Kit/Sdk/TeleportTracker.hpp>
#include <CS2Kit/Sdk/UserCmd.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <CS2Kit/Utils/AngleMath.hpp>
#include <CS2Kit/Utils/DecayingScore.hpp>
#include <CS2Kit/Utils/Log.hpp>
#include <CS2Kit/Utils/SlidingWindowScore.hpp>
#include <CS2Kit/Utils/SlotThrottle.hpp>
#include <CS2Kit/Utils/StringUtils.hpp>
#include <CS2Kit/Utils/TimeUtils.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <CS2Kit/Utils/Validation.hpp>

namespace CS2Kit
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
using Core::ResolvePath;
using Core::Scheduler;
using Core::StageResult;
using Core::StageStatus;
using Core::StandardPluginSettings;
using Core::Subscription;
using Players::ApplyEffect;
using Players::ClearEffect;
using Players::EffectChoice;
using Players::EffectDescriptor;
using Players::EffectInstance;
using Players::EffectScope;
using Players::ParamEffectDescriptor;
using Players::ToggleEffect;

// Sdk
using Sdk::ChatInputCapture;
using Sdk::ClientCvarService;
using Sdk::ClientCvarStatus;
using Sdk::ConVarService;
using Sdk::EntityKeyValues;
using Sdk::EntityOpsService;
using Sdk::EntitySystem;
using Sdk::GameEventService;
using Sdk::GetServerGlobals;
using Sdk::GlowVision;
using Sdk::HasPawnFlag;
using Sdk::InMoveType;
using Sdk::InputHistorySample;
using Sdk::InputHistoryService;
using Sdk::MessageKind;
using Sdk::MessageSystem;
using Sdk::MovementHook;
using Sdk::MoveType;
using Sdk::NetChannelService;
using Sdk::PersistentCenterHtml;
using Sdk::PlayerController;
using Sdk::RawConVar;
using Sdk::ServerCommand;
using Sdk::ServerTick;
using Sdk::ServerTime;
using Sdk::SubtickMove;
using Sdk::TeleportTracker;
using Sdk::UserCmdView;
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
using Players::ParamAction;
using Players::PerSlot;
using Players::Player;
using Players::PlayerManager;
using Players::ResolveTargets;
using Players::TargetError;
using Players::TargetFailure;
using Players::TargetRules;

// Commands (arg factories keep their terse names: CS2Kit::Target(), CS2Kit::Duration(), ...)
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
using Utils::DecayingScore;
using Utils::PairThrottle;
using Utils::ParseDuration;
using Utils::SlidingWindowScore;
using Utils::SlotThrottle;
using Utils::StringUtils;
using Utils::Throttle;
using Utils::TimeUtils;
using Utils::Tokens;
using Utils::Translations;
namespace AngleMath = Utils::AngleMath;
namespace Log = Utils::Log;
namespace Validation = Utils::Validation;

}  // namespace CS2Kit
