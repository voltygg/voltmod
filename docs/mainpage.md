# CS2-Kit {#mainpage}

> **Work in progress** - the API is still moving. Expect breaking changes between versions.

**CS2-Kit** is a C++23 library for building [Counter-Strike 2](https://www.counter-strike.net/) server plugins with [Metamod:Source 2.0](https://www.metamodsource.net/). You describe your plugin's behavior - commands, menus, effects, database rows - as data, and the kit handles the engine: SDK interface resolution, hooks, player tracking, message transport, signature scanning.

A working plugin is a `CommandSpec` and forty lines of skeleton. Generate one with `scripts/new_plugin.py` and go from there (see @ref getting_started).

<h2>Modules</h2>

| Module | Namespace | What's in it |
|--------|-----------|--------------|
| **Core** | `CS2Kit::Core` | `PluginBase` (Metamod lifecycle + your manager container), the `Services`/`Engine()` container, `PluginPolicy`, `Scheduler`, `Registry`, `JsonConfig`, `EffectManager` and effect descriptors, logging |
| **Commands** | `CS2Kit::Commands` | Declarative `CommandSpec` chat commands with typed, pre-resolved arguments |
| **Players** | `CS2Kit::Players` | Connected-player tracking, the target-selector grammar (`@all`, `#slot`, name fragments, ...), single-target `Action` dispatch |
| **Menu** | `CS2Kit::Menu` | WASD center-HTML menus: typed rows, policy-aware context rows, the `Flow` multi-step wizard, presets |
| **Sdk** | `CS2Kit::Sdk` | Engine wrappers: entities, schema, signatures, convars, typed game events, messages (`MessageSystem`), pawn operations, transmit filtering |
| **Database** | `CS2Kit::Database` | Async-first PostgreSQL (`PostgresDatabase`), column-table row mapping, forward-only migrations; opt-in via `CS2KIT_ENABLE_POSTGRES` |
| **Http** | `CS2Kit::Http` | Async `HttpClient` with game-thread completions, config-driven JSON endpoint helpers |
| **Utils** | `CS2Kit::Utils` | Translations with `{token}` substitution, SteamID and duration parsing, chat colors, string/time helpers, angle math, decaying scores, config validation |

<h2>Guides</h2>

- @subpage getting_started - install, scaffold a plugin, build
- @subpage architecture - how the pieces fit: services, policy, lifetimes
- @subpage plugin_guide - the plugin skeleton: `PluginBase`, hooks, events, teardown
- @subpage config_guide - settings files and validation
- @subpage commands_guide - declarative commands and the target selector grammar
- @subpage menus_guide - menus, context rows, and the Flow wizard
- @subpage players_guide - player tracking, targeting, actions, and effects
- @subpage chat_guide - messages, replies, and chat colors
- @subpage sdk_guide - the engine wrapper layer
- @subpage database_guide - async PostgreSQL and row mapping
- @subpage http_guide - async HTTP and JSON REST helpers
- @subpage testing_guide - SDK-free unit tests with doctest

<h2>License</h2>

CS2-Kit is released under the [MIT License](https://github.com/voltygg/cs2-kit/blob/main/LICENSE).
