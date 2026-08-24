# Getting Started {#getting_started}

[TOC]

## Prerequisites

- C++23 compiler (MSVC 2022+, or the Steam Runtime toolchain for Linux)
- CMake 4.3.4+, Conan 2.29.1+, and Ninja - all three are pinned by the `voltmod` Python distribution, so `uv sync` installs them into the project environment (or install globally via pip/pipx)

The HL2SDK and Metamod:Source arrive as Conan packages from a public remote. Nothing is vendored and there are no submodules.

## Start a new project

From an empty directory, let `voltmod init` stamp the whole thing:

```sh
mkdir my-cs2-plugins && cd my-cs2-plugins
git init
uvx --from git+https://github.com/voltygg/voltmod.git voltmod init --plugin my-plugin
uv sync
uv run poe bootstrap
```

It generates:

- `CMakeLists.txt` - a few lines: `project()`, `include(CTest)`, `find_package(voltmod CONFIG REQUIRED)`, and one `add_subdirectory(plugins/<name>)` per plugin. Everything else comes from the kit.
- `CMakePresets.json` - `windows-msvc-{release,debug}` and `linux-steamrt-{release,debug}`, with matching build/test/workflow presets.
- `conanfile.py` - `requires = ("voltmod/[~1]",)`; add your own deps here. cpr, nlohmann_json and (with `with_postgres`) libpqxx arrive transitively.
- `pyproject.toml` - depends on the `voltmod` distribution, which brings the pinned CMake/Conan/Ninja/clang-format and the poe tasks (`build`, `bootstrap`, `new-plugin`, `format`, `lint`). The project carries no build scripts of its own.
- `plugins/my-plugin/` - a working first plugin (see below).

`poe bootstrap` installs the kit's Conan profiles and the public remote, then builds; after that `uv run poe build` is the loop. Output lands in `build/<preset>/plugins/<name>/<platform-arch>/`. Pin the dependency graph with `conan lock create .` (same profiles) and commit `conan.lock` - builds pick it up automatically.

## Scaffold more plugins

```sh
uv run poe new-plugin fun-votes        # from your repo's root
```

You get `plugins/fun-votes/` with a `MetamodPlugin` skeleton, an example command registered from its `App`, a `settings.jsonc` mapped by @ref VoltMod::Core::JsonConfig, and translations - it builds, loads, and answers `!ping` before you write a line of code. The root `CMakeLists.txt` gains its `add_subdirectory` line automatically.

## Plugins are one function call

A plugin's `CMakeLists.txt` is a single declaration - `voltmod_add_plugin` reaches you as a CMakeDeps build module, so it is available right after `find_package(voltmod CONFIG REQUIRED)`:

```cmake
voltmod_add_plugin(fun-votes)
```

- `voltmod_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...] [PCH_HEADERS ...])` creates the Metamod MODULE: `SOURCES` defaults to a recursive glob of `src/*.cpp`; the required HL2SDK translation units (`memoverride.cpp`, `convar.cpp`) and the `VoltMod::VoltMod` link are added for you, along with C++23, the static MSVC runtime, ccache when present, and a precompiled `<VoltMod/Api.hpp>` (extend with `PCH_HEADERS`, disable with `-DVOLTMOD_DISABLE_PCH=ON`).
- `voltmod_install_plugin(<name>)` (called automatically) defines the deploy bundle as an install component: the module under `addons/<name>/bin/{win64|linuxsteamrt64}`, a generated Metamod `.vdf` under `addons/metamod`, the plugin's `configs/`, and the kit's shared gamedata. `cmake --install build/<preset> --component <name> --prefix <dir>` stages a server-ready `addons/` tree.

### Version and build provenance

Every plugin build stamps a generated `<VoltMod/BuildInfo.hpp>` (namespace `VoltMod::BuildInfo`) with a display `Version`, the `RepoCommit` short hash, and the last-commit `BuildDate`. The display version is `<version.txt>+<short-sha>[-dirty]`, where `version.txt` is a single-line file at your repo root (missing file → `0.0.0`). A modified tree is flagged `-dirty`, so those three fields identify a build exactly (which voltmod went in is pinned by `conan.lock`). Wire them into your `Info()` so `meta list` always identifies the exact deployed build:

```cpp
#include <VoltMod/Core/PluginInfoStamp.hpp>  // only from Plugin.cpp - it pulls the per-commit BuildInfo.hpp

VoltMod::PluginInfo MyPlugin::Info() const {
    // WithBuildInfo overwrites Version/Date/Commit from the stamp.
    return VoltMod::WithBuildInfo({ .Name = "My Plugin", .LogTag = "MINE" });
}
```

The stamp reruns every build but rewrites the header only when committed state changes, so no-op builds stay no-op (that is also why `BuildDate` is the last-commit date, not wall-clock time). Outside a git checkout the fields degrade to `"unknown"`; in GitHub Actions, `GITHUB_SHA` is used as a fallback.

### Build-system conventions

The kit's CMake leans on standard mechanisms instead of hand-rolled flags wherever one exists:

- SDK/Metamod headers are `SYSTEM` include dirs, so consumer warning levels don't apply to third-party headers; vendored SDK *sources* compiled into targets (`memoverride.cpp`, `convar.cpp`, the entity2/keyvalues3 TUs, protoc output) are silenced per-source by the `hl2sdk_attach_*` functions that add them.
- Symbol visibility comes from the `CXX_VISIBILITY_PRESET hidden` / `VISIBILITY_INLINES_HIDDEN` target properties, not raw `-fvisibility` flags.
- MSVC Release builds compile with `/Z7` and link with `/DEBUG /OPT:REF /OPT:ICF`, so every shipped plugin has a PDB for crash-dump symbolication (`/Z7` rather than `/Zi` because ccache cannot cache `/Zi`).
- The static-MSVC-runtime and ccache fallbacks live once in `VoltModCommon.cmake` as cache variables (visible to sibling plugin directories); a Conan toolchain that sets them wins.
- SDK includes, defines and ABI flags are the `hl2sdk-cs2` package's usage requirements, not hand-written lists. Warning level is the one exception - `voltmod_set_warnings()` applies it per target, because it is the consumer's policy rather than the SDK's.
- protobuf sources are generated once, inside the `hl2sdk-cs2` package build, and ship as source. No consumer runs protoc.
- Deliberately **not** used, so the audit isn't re-run later: `GenerateExportHeader` (no export macros exist - Metamod's `PLUGIN_EXPOSE` handles the entry point), `install(... RUNTIME_DEPENDENCIES)` (runtimes are static), and `VERSION`/`SOVERSION` (meaningless for MODULE libraries).

## Adding to an existing repo

If you already have a CMake project, add the requirement and the `find_package`:

```python
# conanfile.py
requires = ("voltmod/[~1]",)
```

```cmake
find_package(voltmod CONFIG REQUIRED)
add_subdirectory(plugins/my-plugin)    # voltmod_add_plugin(my-plugin) inside
```

One-time, so Conan can resolve it:

```sh
conan config install https://github.com/voltygg/voltmod.git -sf conan
```

That installs the canonical profiles (`linux-steamrt.txt`, `windows-msvc.txt`) and the public remote together.

## PostgreSQL (optional)

Flip the kit's option; libpqxx arrives transitively and `VoltMod::Database` lights up:

```python
default_options = {"voltmod/*:with_postgres": True}
```

The generated project carries that line already, set to `False`.

## The skeleton, by hand

If you'd rather see what the generator writes: derive from @ref VoltMod::App::MetamodPlugin. The base owns the standard SourceHook hooks and the player lifecycle, creates a @ref VoltMod::Runtime for one load cycle, and hands it to your `OnLoad`. What your plugin owns goes in a struct of your own, built from that runtime and dropped in `OnUnload`.

> **Short names.** Including `<VoltMod/Api.hpp>` hoists the public vocabulary to `VoltMod::Type`
> (`VoltMod::MetamodPlugin`, `VoltMod::CommandSpec`, `VoltMod::Runtime`, ...), so you don't spell out
> the internal module namespaces. The fully-qualified `VoltMod::Module::Type` forms keep working.
> Examples in these guides use the short form.

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginInfoStamp.hpp>

namespace MyNs
{
struct App
{
    explicit App(VoltMod::Runtime& runtime) : Runtime(runtime) {}

    bool Start() { return VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "my-plugin"}); }

    VoltMod::Runtime& Runtime;
    ConfigManager Config;   // your own state, in dependency order
};
}  // namespace MyNs

class MyPlugin final : public VoltMod::MetamodPlugin
{
protected:
    VoltMod::PluginInfo Info() const override
    {
        return VoltMod::WithBuildInfo({ .Name = "My Plugin", .Author = "me", .LogTag = "MINE" });
    }

    bool OnLoad(VoltMod::Runtime& runtime, bool late) override
    {
        _app.emplace(runtime);
        return _app->Start();
    }

    void OnUnload() override { _app.reset(); }

private:
    std::optional<MyNs::App> _app;
};

// Global instance and PLUGIN_EXPOSE in one line:
VOLTMOD_PLUGIN(MyPlugin);
```

## Doing it without the base

If you can't derive from `MetamodPlugin`, own a `VoltMod::Runtime` yourself: construct it and call `Start(LoadContext{...})` in `Load()`, `OnGameFrame()` from your frame hook, `OnPlayerDisconnect(slot)` from disconnect handling, and destroy it in `Unload()`. Its destructor is the kit's shutdown.

## Next steps

- @ref plugin_guide - what the base owns and what you override
- @ref commands_guide - add real commands
- @ref config_guide - grow the settings file
- @ref menus_guide - menus and wizards
- @ref testing_guide - unit-test the pure logic you extract
