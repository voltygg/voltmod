# Choosing a CS2 plugin framework {#framework_comparison}

[TOC]

VoltMod, SwiftlyS2, Plugify, and CounterStrikeSharp all support
Counter-Strike 2 server plugins, but they solve different workflow problems.
Choose by language, runtime model, required services, and how much engine
integration you want the framework to own.

This page compares documented features as of August 2026. It is not a
performance benchmark. "Language ecosystem" means the plugin can use normal
packages from its language, but the framework does not provide one shared
service with a framework-specific lifecycle.

## Summary

| Framework | Best fit |
| --- | --- |
| **VoltMod** | Native C++ plugins that want explicit ownership plus shared command, menu, engine, HTTP, PostgreSQL, build, and packaging infrastructure |
| **SwiftlyS2** | C# plugins that want a broad first-party Source 2 API, dependency injection, database connections, advanced menus, and automatic hot reload |
| **Plugify with S2SDK** | Teams that need several plugin languages or calls between plugins written in different languages |
| **CounterStrikeSharp** | C# plugins that want a familiar .NET 8 and NuGet workflow with a large, direct CS2 API |

## Feature matrix

| Capability | VoltMod | SwiftlyS2 | Plugify with S2SDK | CounterStrikeSharp |
| --- | --- | --- | --- | --- |
| Plugin language | C++23 | C# on .NET 10 | C++, C#, Python, Go, Lua, Rust, JavaScript/TypeScript, and additional language modules | C# on .NET 8 |
| Host model | Each plugin is a native Metamod module | Managed plugins run over a C++ Source 2 core | Plugify hosts language modules; Metamod and S2SDK provide Source 2 integration | A Metamod plugin hosts the .NET scripting layer |
| Commands | Declarative specs, aliases, surfaces, typed arguments, targeting, and policy | Console/chat commands, aliases, hooks, and framework permissions | Console commands and hooks through S2SDK | Console/server/chat commands and command attributes |
| Menus | WASD center-HTML menus, typed rows, pickers, and multi-step flows | Built-in builder with buttons, inputs, sliders, choices, toggles, and more | S2SDK exposes user-message and UI primitives; no equivalent high-level workflow is documented as part of the core stack | Chat and center-HTML menu APIs |
| Events and hooks | Typed game events, listeners, SourceHook helpers, movement hooks, and input history | Generated typed game events, listeners, function hooks, net messages, and entity input/output hooks | Game events, listeners, function hooks, user messages, and other Source 2 hooks through S2SDK | Game-event handlers, listeners, timers, virtual functions, and memory/dynamic hooks |
| Gamedata | Versioned JSONC with a schema, split into signatures, rel32 addresses, vtable slots and field offsets; C++ owns every prototype and field type through typed `Bindings`, and each entry that fails to resolve turns off one named capability with its reason | Generated game data with signature/offset lookups resolved by name at runtime | Signature and offset lookups through S2SDK's own game data | Game data JSON with named signatures and offsets, resolved by name at call time |
| Entities and schemas | Typed entity handles, schema fields, pawn operations, state changes, and transmit filters | Entity creation/query, generated schema types, handles, inputs, and outputs | Handle-based entities and schema get/set APIs through S2SDK | Generated schema classes, entity utilities, handles, and state-change helpers |
| Configuration and localization | JSONC loading, schemas, startup validation, per-player translations, and chat colors | JSON, JSONC, and TOML options plus localization; configuration can reload | Configuration is supplied by Plugify extensions or the selected language stack | Typed JSON configuration and JSON localization with per-player cultures |
| Database | Optional async PostgreSQL service, migrations, and row mapping | Shared connections for MySQL, PostgreSQL, and SQLite | No common Source 2 database service is documented; use a language package or another Plugify extension | Use normal .NET database packages; SQLite is shown in project examples |
| HTTP | Async framework client with game-thread completions and JSON endpoint helpers | Use .NET HTTP libraries and framework scheduling as needed | Use the selected language's HTTP packages | Use .NET HTTP libraries |
| Cross-plugin API | Typed `ServiceExchange` interfaces | Shared interfaces and plugin services | Cross-language exported methods are a core Plugify feature | Typed player and plugin capabilities with shared contract assemblies |
| Reload model | Metamod load/unload cycle with runtime-scoped cleanup; no automatic file watcher | Automatic plugin hot reload is configurable and enabled by default | S2SDK documents hot-reloading for development | Updated plugin DLLs reload automatically when enabled |
| Project tooling | `init`, `new-plugin`, `doctor`, pinned CMake/Conan/Ninja, presets, CTest, install components, and package publishing | `dotnet new` plugin template and `dotnet publish` | Mamba packages, manifests, language-module tooling, and project-specific manager commands | Standard `dotnet`/NuGet build plus framework examples and API packages |

## Where VoltMod differs

VoltMod's plugin is the native Metamod module. There is no managed host between
plugin code and the framework, and no multi-language ABI to cross. That model
fits codebases that want deterministic C++ ownership and direct control over
SDK interactions.

Its higher-level services are intentionally opinionated:

- command arguments and targets resolve before the handler;
- a plugin injects permission and immunity policy once;
- menu flows carry typed state across steps;
- registrations return subscriptions tied to a load cycle;
- HTTP and database completions return to the game thread;
- CMake and Conan produce a reproducible native addon bundle.

The tradeoff is the native C++ toolchain and compile-deploy-reload loop.
SwiftlyS2 and CounterStrikeSharp offer a faster managed-code edit loop.
Plugify offers the broadest language choice and cross-language interoperability.

## Selection guide

Choose VoltMod when:

- the plugin should be a native C++ Metamod module;
- ownership, lifetime, and unload behavior must stay explicit;
- PostgreSQL, HTTP, commands, targeting, menus, and packaging should share one
  framework lifecycle;
- the team wants Conan/CMake presets and SDK-free CTest support.

Choose SwiftlyS2 when:

- the team wants C# and .NET 10;
- advanced first-party menu controls and shared database connections matter;
- automatic hot reload and a broad injected service surface fit the project.

Choose Plugify with S2SDK when:

- plugins must be written in several languages;
- plugins in different languages need to call each other;
- package-managed language modules are more important than one opinionated CS2
  application framework.

Choose CounterStrikeSharp when:

- the team wants C# and .NET 8;
- the standard NuGet ecosystem should supply database, HTTP, and other
  application libraries;
- automatic DLL reload, attributes, capabilities, and the established
  CounterStrikeSharp API fit the plugin.

## Official sources

The comparison uses each project's own documentation:

- [SwiftlyS2 repository and feature list](https://github.com/swiftly-solution/swiftlys2)
- [SwiftlyS2 core services](https://swiftlys2.net/docs/development/swiftly-core/)
- [SwiftlyS2 plugin template](https://swiftlys2.net/docs/development/getting-started/)
- [SwiftlyS2 core configuration](https://swiftlys2.net/docs/resources/core-configuration/)
- [Plugify overview](https://plugify.net/introduction/overview/)
- [Plugify language modules](https://plugify.net/)
- [Plugify S2SDK features](https://plugify.net/plugins/s2sdk/features/)
- [Plugify S2SDK command guide](https://plugify.net/plugins/s2sdk/guides/console-commands/)
- [CounterStrikeSharp repository](https://github.com/roflmuffin/counterstrikesharp)
- [CounterStrikeSharp hello-world and reload guide](https://docs.cssharp.dev/docs/guides/hello-world-plugin.html)
- [CounterStrikeSharp menu API](https://docs.cssharp.dev/api/CounterStrikeSharp.API.Modules.Menu.MenuManager.html)
- [CounterStrikeSharp shared plugin API](https://docs.cssharp.dev/docs/features/shared-plugin-api.html)
- [CounterStrikeSharp configuration reference](https://docs.cssharp.dev/docs/reference/core-configuration.html)

Features and runtime versions can change. Recheck these sources before making a
long-term platform decision.
