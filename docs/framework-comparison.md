# CS2 plugin frameworks {#framework_comparison}

[TOC]

A comparison of the language, loader, and build setup of four CS2 plugin frameworks. Performance
and stability are not ranked.

Last checked: 2026-09-20.

## Overview

| Framework | Plugin language | Loader | Build setup | License |
| --- | --- | --- | --- | --- |
| VoltMod | C++23 | Metamod:Source | `voltmod init`, Conan, and CMake | MIT |
| [CounterStrikeSharp](https://github.com/roflmuffin/CounterStrikeSharp) | C# on .NET 8 | Metamod:Source | .NET class library | GPLv3 with a plugin exception |
| [SwiftlyS2](https://swiftlys2.net/) | C# on .NET 10 | [SwiftlyS2 loader](https://swiftlys2.net/docs/installation/) | `dotnet new` template and `dotnet publish` | GPLv3 with a plugin exception |
| [Plugify for Source 2](https://github.com/untrustedmodders/plugify-plugin-s2sdk) | C++, C#, Go, Python, JavaScript, Lua, Rust, D, and other language modules | Plugify with the Source 2 SDK plugin | Depends on the installed language module | Plugify is MIT; the Source 2 SDK plugin is GPLv3 |

## VoltMod

VoltMod currently builds native C++23 plugins. One Metamod:Source host loads their manifests,
checks dependencies, and handles `load`, `unload`, and `reload` commands. The framework includes
commands, players, menus, Panorama UI, engine APIs, HTTP, three database drivers, Conan packages,
and project tools.

The host and runtime are structured to allow more language bindings later, but only the C++23 SDK
is available today. On Windows, a loaded DLL must be unloaded before it can be replaced.

## CounterStrikeSharp

CounterStrikeSharp runs C# plugins on .NET 8 through a Metamod:Source plugin. Its documented APIs
cover commands, game events, timers, client and map listeners, server information, schema access,
and administration. Replacing a plugin DLL triggers
[automatic hot reload](https://docs.cssharp.dev/docs/guides/hello-world-plugin.html) by default.

The server needs the supported .NET runtime. The first installation normally uses the release
archive that includes it. See the official [installation guide](https://github.com/roflmuffin/CounterStrikeSharp/blob/main/INSTALL.md).

## SwiftlyS2

SwiftlyS2 has a native core and a C# plugin API. It uses its own loader, so Metamod:Source is not
required. Its APIs cover commands, convars, entities, events, hooks, protobuf messages,
permissions, translations, timers, traces, profiling, databases, menus, and sound.

Plugins start from the official .NET template and build with `dotnet publish`. See the
[development guide](https://swiftlys2.net/docs/development/getting-started).

## Plugify for Source 2

Plugify loads language modules and lets plugins written in different languages call one another.
Source 2 support comes from a separate SDK plugin. A server installs the Plugify host, the Source
2 SDK plugin, and the language modules its plugins need.

Plugify can run through the [Source 2 launcher/runtime](https://github.com/untrustedmodders/plugify-source2-launcher)
or a [Metamod loader](https://github.com/untrustedmodders/plugify-metamod-loader). See the official
[installation guide](https://plugify.net/use-cases/standalone-launcher/installation/).

## Main differences

- VoltMod is native C++23 today and includes its build, database, Panorama, and server tools.
- CounterStrikeSharp and SwiftlyS2 use managed C# plugins.
- SwiftlyS2 uses its own loader; VoltMod and CounterStrikeSharp use Metamod:Source.
- Plugify supports several languages through separate modules.

Future VoltMod language bindings are not included in this comparison until they ship with a
documented SDK and compatibility contract.
