# Getting Started {#getting_started}

[TOC]

## Prerequisites

- C++23 compiler (MSVC 2022+, or the Steam Runtime toolchain for Linux)
- CMake 4.3.4+, Conan 2.29.1+, and Ninja - all three are pinned in `pyproject.toml`, so `uv sync` installs them into the project environment (or install globally via pip/pipx)

The HL2SDK and Metamod:Source come with the kit as submodules - you don't install them separately.

## Start a new project

From an empty directory, vendor the kit and let `init_project.py` stamp the whole project:

```sh
mkdir my-cs2-plugins && cd my-cs2-plugins
git init
git submodule add https://github.com/suxrobgm/cs2-kit.git vendor/cs2-kit
git submodule update --init --recursive
python vendor/cs2-kit/scripts/init_project.py --plugin my-plugin
uv sync
uv run poe build
```

The init script needs only a system Python 3. It generates:

- `CMakeLists.txt` - a few lines: `project()`, `include(CTest)`, `add_subdirectory(vendor/cs2-kit)`, and one `add_subdirectory(plugins/<name>)` per plugin. Everything else comes from the kit.
- `CMakePresets.json` - one line that includes the kit's presets (`windows-msvc-{release,debug}`, `linux-steamrt-{release,debug}`, with matching build/test/workflow presets).
- `conanfile.py` - the third-party deps (cpr, nlohmann_json); add your own here. The Conan profiles under `vendor/cs2-kit/conan/profiles/` are canonical and picked up automatically.
- `pyproject.toml` - pins CMake/Conan/Ninja/clang-format via uv, and poe tasks (`build`, `bootstrap`, `new-plugin`, `format`, `lint`) that call the kit's scripts directly - the project carries no build scripts of its own.
- `plugins/my-plugin/` - a working first plugin (see below).

`uv run poe build` runs `conan install` plus the CMake workflow preset; output lands in `build/<preset>/plugins/<name>/<platform-arch>/`. Once the first build works, you can pin the dependency graph with `conan lock create .` (using the same profiles) and commit the resulting `conan.lock` - builds pick it up automatically.

## Scaffold more plugins

```sh
uv run poe new-plugin fun-votes        # from your repo's root
```

You get `plugins/fun-votes/` with a `PluginBase` skeleton, a self-registering example command, a `settings.jsonc` mapped by @ref CS2Kit::Core::JsonConfig, and translations - it builds, loads, and answers `!ping` before you write a line of code. The root `CMakeLists.txt` gains its `add_subdirectory` line automatically.

## Plugins are one function call

A plugin's `CMakeLists.txt` is a single declaration - `cs2_add_plugin` is defined by the kit and available after `add_subdirectory(vendor/cs2-kit)`:

```cmake
cs2_add_plugin(fun-votes
    LIBRARIES nlohmann_json::nlohmann_json
)
```

- `cs2_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...])` creates the Metamod MODULE: `SOURCES` defaults to a recursive glob of `src/*.cpp`; the required HL2SDK translation units (`memoverride.cpp`, `convar.cpp`) and the `CS2Kit::CS2Kit` link are added for you, along with C++23, the static MSVC runtime, and ccache when present.
- `cs2_install_plugin(<name>)` (called automatically) defines the deploy bundle as an install component: the module under `addons/<name>/bin/{win64|linuxsteamrt64}`, a generated Metamod `.vdf` under `addons/metamod`, the plugin's `configs/`, and the kit's shared gamedata. `cmake --install build/<preset> --component <name> --prefix <dir>` stages a server-ready `addons/` tree.

### Version and build provenance

Every plugin build stamps a generated `<CS2Kit/BuildInfo.hpp>` (namespace `CS2Kit::BuildInfo`) with a display `Version`, the `RepoCommit` short hash, and the last-commit `BuildDate`. The display version is `<version.txt>+<short-sha>[-dirty]`, where `version.txt` is a single-line file at your repo root (missing file → `0.0.0`). A clean `RepoCommit` pins your submodules (cs2-kit included), and a modified one is flagged `-dirty`, so those three fields identify a build exactly. Wire them into your `Info()` so `meta list` always identifies the exact deployed build:

```cpp
#include <CS2Kit/BuildInfo.hpp>  // only from Plugin.cpp - the header changes every commit

CS2Kit::PluginInfo MyPlugin::Info() const {
    return { .Name = "My Plugin",
             .Version = CS2Kit::BuildInfo::Version,
             .Date = CS2Kit::BuildInfo::BuildDate,
             .Commit = CS2Kit::BuildInfo::RepoCommit,
             .LogTag = "MINE" };
}
```

The stamp reruns every build but rewrites the header only when committed state changes, so no-op builds stay no-op (that is also why `BuildDate` is the last-commit date, not wall-clock time). Outside a git checkout the fields degrade to `"unknown"`; in GitHub Actions, `GITHUB_SHA` is used as a fallback.

### Build-system conventions

The kit's CMake leans on standard mechanisms instead of hand-rolled flags wherever one exists:

- SDK/Metamod headers are `SYSTEM` include dirs, so consumer warning levels don't apply to third-party headers; vendored SDK *sources* compiled into targets (`memoverride.cpp`, `convar.cpp`, the entity2/keyvalues3 TUs, protoc output) are silenced per-source via `cs2kit_mark_vendored_sources`.
- Symbol visibility comes from the `CXX_VISIBILITY_PRESET hidden` / `VISIBILITY_INLINES_HIDDEN` target properties, not raw `-fvisibility` flags.
- MSVC Release builds compile with `/Z7` and link with `/DEBUG /OPT:REF /OPT:ICF`, so every shipped plugin has a PDB for crash-dump symbolication (`/Z7` rather than `/Zi` because ccache cannot cache `/Zi`).
- The static-MSVC-runtime and ccache fallbacks live once in `CS2KitSdk.cmake` as cache variables (visible to sibling plugin directories); a Conan toolchain that sets them wins.
- Deliberately **not** used, so the audit isn't re-run later: `GenerateExportHeader` (no export macros exist - Metamod's `PLUGIN_EXPOSE` handles the entry point), `CMakePackageConfigHelpers` (the kit is consumed via `add_subdirectory`, never installed as a package), `install(... RUNTIME_DEPENDENCIES)` (runtimes are static), `VERSION`/`SOVERSION` (meaningless for MODULE libraries), and `protobuf_generate()` (it expects the protobuf CMake package; wiring it to the SDK's bundled protoc and the flat `cs_usercmd` proto layout would be more code than the small custom function).

## Adding to an existing repo

If you already have a CMake project, you only need the submodule and the subdirectory:

```sh
git submodule add https://github.com/suxrobgm/cs2-kit.git vendor/cs2-kit
git submodule update --init --recursive
```

```cmake
add_subdirectory(vendor/cs2-kit)
add_subdirectory(plugins/my-plugin)    # cs2_add_plugin(my-plugin) inside
```

Run Conan before configuring (the kit's targets resolve cpr/nlohmann_json through `find_package`), or drive everything through `python vendor/cs2-kit/scripts/build.py`, which works from any repo root that vendors the kit.

## PostgreSQL (optional)

Enable the kit's async Postgres client by setting `CS2KIT_ENABLE_POSTGRES` before the subdirectory and adding the driver to your Conan requires:

```cmake
set(CS2KIT_ENABLE_POSTGRES ON)
add_subdirectory(vendor/cs2-kit)
```

```python
requires = ("cpr/1.11.2", "nlohmann_json/3.11.3", "libpqxx/7.10.0")
```

The generated project carries both lines commented out, at exactly the spots to edit.

## The skeleton, by hand

If you'd rather see what the generator writes: derive from @ref CS2Kit::Core::PluginBase with your manager struct. The base runs `CS2Kit::Initialize()`/`Shutdown()`, owns the standard SourceHook hooks and the player lifecycle, constructs your managers once the kit services are live, and tears everything down LIFO on unload.

> **Short names.** Including `<CS2Kit/Api.hpp>` hoists the public vocabulary to `CS2Kit::Type`
> (`CS2Kit::PluginBase`, `CS2Kit::CommandSpec`, `CS2Kit::Engine()`, ...), so you don't spell out
> the internal module namespaces. The fully-qualified `CS2Kit::Module::Type` forms keep working.
> Examples in these guides use the short form.

```cpp
#include <CS2Kit/Api.hpp>

struct Managers
{
    ConfigManager Config;   // your managers, in dependency order
};
Managers& App();            // free accessor, forwarded below

class MyPlugin : public CS2Kit::PluginBase<Managers>
{
protected:
    CS2Kit::PluginInfo Info() const override
    {
        return { .Name = "My Plugin", .Author = "me", .Version = "1.0.0", .LogTag = "MINE" };
    }

    bool OnLoad(bool late) override
    {
        return App().Config.Load("addons/my-plugin/configs/settings.jsonc");
    }
};

MyPlugin g_MyPlugin;
PLUGIN_EXPOSE(MyPlugin, g_MyPlugin);

Managers& App() { return MyPlugin::App(); }
```

## Manual initialization

If you can't derive from the base, call the entry points from your own `ISmmPlugin`: `CS2Kit::Initialize()` in `Load()`, `CS2Kit::OnGameFrame()` from your frame hook, `CS2Kit::OnPlayerDisconnect()` from disconnect handling, and `CS2Kit::Shutdown()` in `Unload()`.

## Next steps

- @ref plugin_guide - what the base owns and what you override
- @ref commands_guide - add real commands
- @ref config_guide - grow the settings file
- @ref menus_guide - menus and wizards
- @ref testing_guide - unit-test the pure logic you extract
