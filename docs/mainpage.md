# VoltMod {#mainpage}

> **Work in progress** - the API is still moving. Expect breaking changes between versions.

**VoltMod** is a C++23 framework for building [Counter-Strike 2](https://www.counter-strike.net/) server plugins with [Metamod:Source 2.0](https://www.metamodsource.net/). You describe commands, menus, effects, and database rows as data. The framework handles SDK interfaces, hooks, players, messages, and signatures through one @ref VoltMod::Runtime for each plugin load.

Generate a working skeleton with `voltmod new-plugin`; see @ref getting_started.

<h2>Modules</h2>

| Module | Namespace | What's in it |
|--------|-----------|--------------|
| **Core** | `VoltMod::Core` | `PluginPolicy`, `Scheduler`, `SlotEvents`, `Subscription`, `EffectManager` and effect descriptors, logging, translations with `{token}` substitution, SteamID and duration parsing, chat colors, string/time helpers, angle math, decaying scores, config validation |
| **Commands** | `VoltMod::Commands` | Declarative `CommandSpec` chat commands with typed, pre-resolved arguments |
| **Players** | `VoltMod::Players` | Connected-player tracking, the target-selector grammar (`@all`, `#slot`, name fragments, ...), single-target `Action` dispatch |
| **Menu** | `VoltMod::Menu` | WASD center-HTML menus: typed rows, policy-aware context rows, the `Flow` multi-step wizard, presets |
| **Sdk** | `VoltMod::Sdk` | Engine wrappers: entities, schema, signatures, convars, typed game events, messages (`MessageSystem`), pawn operations, transmit filtering |
| **Database** | `VoltMod::Database` | Async-first PostgreSQL (`PostgresDatabase`), column-table row mapping, forward-only migrations; opt-in via `VOLTMOD_ENABLE_POSTGRES` |
| **Http** | `VoltMod::Http` | Async `HttpClient` with game-thread completions, config-driven JSON endpoint helpers |
| **App** | `VoltMod::App` | The composition root: `MetamodPlugin` (Metamod lifecycle), `JsonConfig`, `StatusService`, `ServiceExchange` for cross-plugin interfaces |

<h2>Guides</h2>

- @subpage getting_started - install, scaffold a plugin, build
- @subpage architecture - how the pieces fit: the `Runtime`, policy, lifetimes
- @subpage plugin_guide - the plugin skeleton: `MetamodPlugin`, hooks, events, teardown
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

VoltMod is released under the [MIT License](https://github.com/voltygg/voltmod/blob/main/LICENSE).
