# VoltMod {#mainpage}

> VoltMod is under active development. Public APIs may change between
> versions.

VoltMod is a native C++23 framework for Counter-Strike 2 server plugins on
Metamod:Source. It provides one @ref VoltMod::Runtime for each plugin load and
shared services for commands, players, menus, messages, engine access, HTTP,
and optional PostgreSQL.

Start with @ref getting_started to generate a plugin that builds, loads, and
answers `!ping`.

<h2>Modules</h2>

Every public name lives directly in `VoltMod`. A module is a source directory and a
layer in the include graph, not a namespace: moving a type between modules never
renames it. `include/VoltMod/<Module>/` is where a header lives, and the
`VoltMod::` spelling is what you write.

| Module | Header directory | Purpose |
| --- | --- | --- |
| Core | `VoltMod/Core/` | Policy, scheduling, subscriptions, effects, logging, translations, parsing, config validation, and utilities |
| Commands | `VoltMod/Commands/` | Declarative commands with typed, pre-resolved arguments |
| Players | `VoltMod/Players/` | Player tracking, target selectors, and actions |
| Menu | `VoltMod/Menu/` | WASD center-HTML menus, context rows, pickers, and flows |
| Engine | `VoltMod/Engine/` | Interfaces, gamedata and its typed bindings, `ConVar<T>`, the server clock, maps, precaching, and console commands |
| Entities | `VoltMod/Entities/` | Entity lookup, the typed player controller, schema fields, items, and pawn operations |
| Events | `VoltMod/Events/` | The game event listener service and its typed event structs |
| Messaging | `VoltMod/Messaging/` | Chat and center-HTML messages, sticky panels, chat colors, and the vote panel |
| Hooks | `VoltMod/Hooks/` | Movement, damage, transmit, teleport, chat-input and client-convar hooks, and workshop addon delivery |
| Hud | `VoltMod/Hud/` | Panorama `custom_hud_layout` panels and the button presses they send back |
| Unsafe | `VoltMod/Unsafe/` | Opt-in raw hooking: `VOLTMOD_SCOPED_HOOK`, and `VOLTMOD_VHOOK` + `VtableHook` for a vtable slot |
| Database | `VoltMod/Database/` | Optional async PostgreSQL, migrations, and row mapping |
| Http | `VoltMod/Http/` | Async HTTP and configured JSON endpoints |
| App | `VoltMod/App/` | Metamod lifecycle, JSONC loading, status, and cross-plugin services |

<h2>Guides</h2>

- @subpage getting_started - create, build, stage, and verify a plugin
- @subpage framework_comparison - compare VoltMod with other CS2 frameworks
- @subpage architecture - runtime, policy, modules, and lifetimes
- @subpage plugin_guide - plugin lifecycle and ownership
- @subpage config_guide - settings and validation
- @subpage commands_guide - commands and targeting
- @subpage menus_guide - menus and multi-step flows
- @subpage custom_hud_guide - Panorama HUD layouts and button presses
- @subpage workshop_guide - making clients download workshop addons
- @subpage players_guide - players, actions, and effects
- @subpage chat_guide - messages, replies, and chat colors
- @subpage sdk_guide - engine wrappers
- @subpage database_guide - PostgreSQL
- @subpage http_guide - HTTP and JSON endpoints
- @subpage testing_guide - SDK-free doctest tests

<h2>License</h2>

VoltMod is released under the
[MIT License](https://github.com/voltygg/voltmod/blob/main/LICENSE).
