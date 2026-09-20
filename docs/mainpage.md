# VoltMod {#mainpage}

VoltMod is a C++23 framework for Counter-Strike 2 server plugins on Metamod:Source. One
process-wide host, `voltmod.dll` / `voltmod.so`, is the server's only Metamod plugin and loads
your plugins from `addons/voltmod/plugins/<name>/`. Each plugin gets one @ref VoltMod::Runtime per load cycle
with commands, players, menus, messages, engine access, HTTP and an optional database.

Public APIs may change between versions.

## Start here

- @subpage getting_started - create a plugin, build it, install it, load it
- @subpage plugin_guide - the plugin entry point, `plugin.json`, load steps, logging, install layout
- @subpage host_guide - the `volt` commands, dependencies, refusals, troubleshooting
- @subpage architecture - modules, the host/plugin model, lifetimes and boundaries
- @subpage framework_comparison - compare languages, runtimes and tooling with other CS2 frameworks

## Writing a plugin

- @subpage config_guide - `Options<Settings>`, validation, reloading, translations
- @subpage commands_guide - chat and console commands, typed arguments, targeting
- @subpage players_guide - the roster, permissions and immunity, actions and effects
- @subpage chat_guide - messages, replies and chat colors
- @subpage menus_guide - menus and multi-step flows
- @subpage custom_ui_guide - Panorama `custom_hud_layout` panels and their button presses
- @subpage workshop_guide - making clients download workshop addons
- @subpage http_guide - the async HTTP client and JSON endpoints
- @subpage database_guide - Postgres, MariaDB and SQLite with migrations

## Game API

- @subpage sdk_guide - what each SDK aggregate header brings in, and availability
  - @ref sdk_gamedata_guide - the gamedata file, typed bindings, baked schema fields
  - @ref sdk_players_guide - entity lookup, the player wrapper, pawn operations
  - @ref sdk_visibility_guide - render tricks, per-recipient visibility, glow vision
  - @ref sdk_messaging_guide - messages, sticky panels, chat input, the vote panel
  - @ref sdk_events_guide - typed convars, game event listeners, level changes
  - @ref sdk_hooks_guide - movement and teleport hooks, custom vtable hooks, console commands
  - @ref sdk_client_telemetry_guide - the server clock, latency, client convar queries

## Tooling

- @subpage conan_guide - the Conan package, CMake targets and functions, publishing
- @subpage panorama_guide - Jinja screens and the render/check/compile/publish pipeline
- @subpage testing_guide - SDK-free doctest tests

## License

VoltMod is released under the
[MIT License](https://github.com/voltygg/voltmod/blob/main/LICENSE).
