#pragma once

#include <CS2Kit/App/PluginIdentity.hpp>
#include <CS2Kit/App/ServiceExchange.hpp>
#include <CS2Kit/App/StatusService.hpp>
#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Core/CoreServices.hpp>
#include <CS2Kit/Http/HttpClient.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>
#include <CS2Kit/Utils/UtilsServices.hpp>

namespace CS2Kit::App
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

    // Declaration order == construction order; destruction is the reverse.
    /** Primitives every other layer may reach: policy, scheduler, load report, slot signal. */
    Core::CoreServices Core;
    /** Translations. */
    Utils::UtilsServices Utils;
    /** Every engine-facing service. */
    Sdk::SdkServices Sdk{Core};

    /** Interfaces offered to, and borrowed from, other plugins. */
    App::ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig. */
    App::PluginIdentity Identity;
    /** Status sections for diagnostics commands; kit sections registered during load. */
    App::StatusService Status;

    Players::PlayerManager Players{Core.Slots};
    Commands::CommandManager Commands;
    Menu::MenuManager Menus;
    /** Completions dispatch on the game thread from the OnGameFrame pump; Shutdown stops it. */
    Http::HttpClient Http;

    /** Internal schema-offset service (forward-declared type). */
    CS2Kit::Sdk::SchemaService& Schema() { return Sdk.Schema(); }
};

/** Set/clear the active Services backing @ref Engine(). Called by MetamodPluginBase on Load/Unload. */
void SetActiveServices(Services* services);

/** The active Services. Asserts if called outside a Load/Unload window. */
Services& Engine();

/** The active Services, or nullptr - for teardown paths that may run after Shutdown. */
Services* EngineOrNull();

}  // namespace CS2Kit::App
