#pragma once

#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/PluginManifest.hpp>
#include <CS2Kit/Core/PluginPolicy.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/ServiceExchange.hpp>
#include <CS2Kit/Core/SlotEvents.hpp>
#include <CS2Kit/Core/StatusService.hpp>
#include <CS2Kit/Http/HttpClient.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <string>

namespace CS2Kit::Core
{

/**
 * @brief Owns every CS2Kit service for one Load/Unload cycle.
 *
 * Replaces the per-class process-lifetime singletons: the plugin constructs one
 * Services on Load and destroys it on Unload, so service state cannot leak across
 * `meta unload`/`meta reload`. Members are declared in dependency order;
 * destruction is the reverse (RAII).
 *
 * Reach a service via @ref Engine() (e.g. `Engine().Players`, `Engine().Sdk.Schema()`).
 */
class Services
{
public:
    Services();
    ~Services();
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    // Declaration order == construction order.
    /** Plugin-supplied policy (permissions, targeting, replies). Set once in OnLoad. */
    PluginPolicy Policy;
    /** Interfaces offered to, and borrowed from, other plugins. Declared first so it
     *  outlives everything that publishes into it. */
    Core::ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig. */
    Core::PluginIdentity Identity;
    /** Named/timed load stages recorded by Initialize and the plugin's OnLoad. */
    Core::LoadReport LoadReport;
    /** Map the server is running, captured from the StartupServer hook. Empty after a late
     *  (mid-map) load until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;
    /** Status sections for diagnostics commands; kit sections registered during load. */
    Core::StatusService Status;
    /** "This slot changed hands", raised by Players and consumed by services beneath it.
     *  Declared here so it outlives every listener below. */
    Core::SlotEvents Slots;
    Core::Scheduler Scheduler;
    Utils::Translations Translations;

    /** Every engine-facing service, as one lower-layer unit. */
    Sdk::SdkServices Sdk{Scheduler, Slots, Translations};

    Players::PlayerManager Players{Slots};
    Commands::CommandManager Commands;
    Menu::MenuManager Menus;
    /** Completions dispatch on the game thread from the OnGameFrame pump; Shutdown stops it. */
    Http::HttpClient Http;
};

/** Set/clear the active Services backing @ref Engine(). Called by MetamodPluginBase on Load/Unload. */
void SetActiveServices(Services* services);

/** The active Services. Asserts if called outside a Load/Unload window. */
Services& Engine();

/** The active Services, or nullptr - for teardown paths that may run after Shutdown. */
Services* EngineOrNull();

}  // namespace CS2Kit::Core
