<!-- markdownlint-configure-file { "MD024": { "siblings_only": true } } -->

# Changelog

What changed in each VoltMod release. Older history is in git.

## Unreleased

### Breaking

- Drop `VOLTMOD_PLUGIN` and `<VoltMod/App/PluginEntry.hpp>`: `voltmod_add_plugin` generates the entry
  point for the `<Namespace>::App` in `src/App.hpp`.
- Replace the App constructor with `using Plugin::Plugin;` and load settings in the member initializer,
  `ConfigManager Config = VoltMod::LoadConfig<ConfigManager>(Runtime);`. `Load` is optional, and a
  failed settings file refuses the plugin before it runs.
- Subscribe to `Runtime.Map.Started` and `Runtime.Players.Said` where you overrode `OnServerStartup`
  and `OnPlayerChat`; menu input and `!` commands are handled before `Said`.
- Keep the `Subscription` that `Exchange.Publish` returns; `Unpublish` is gone.
- Call `Runtime.UsePanorama(layout, addonId)` where you built a `PanoramaMenu` from
  `PanoramaMenuServices()` and called `Menus.Prefer`.
- Command handlers need no `-> Result<Reply>` unless they mix `Reply::Silent()` with `Ok`/`Fail`, and
  may return nothing. `Caller::Tr` is `Caller::Translations`.
- `Messages::Reply` and `ReplyKey` are `Send` and `SendKey`.
- `Policy::HasPermission` asks the plugin that publishes `VoltMod::IPermissions` by default; publish it
  instead of installing a policy per plugin. `CommandsMissingPolicy` and the `Permissions` load step
  are gone.
- `Action`, `ActionDispatcher`, `EffectDescriptor`, `EffectDispatcher`, `EffectManager` and
  `ActionRows` left the framework, and `Policy::Broadcast` with them.

- Game event structs are generated for every event: the player an event is about is `Slot`
  (`PlayerDeath::VictimSlot` and `PlayerHurt::VictimSlot` included), `PlayerHurt::Hitbox` is `Hitgroup`,
  `PlayerTeam::OldTeam` is `Oldteam`, `BulletImpact::Slot` is `ShooterSlot`, and each struct's `Name` is
  `EventName`. `GameEvents::On` drops an event whose `Slot` is invalid.
- `ConVars::ExecuteClientCommand(slot, cmd)` is `Controller::ExecuteCommand(cmd)`. `Logger<T>` and
  `EntitySystem::AlivePawns()` are gone.
- Rebuild every plugin: the host ABI is now 5 (`IHostEvents::OnClientConnecting`).

### New

- `Runtime.Players.Connecting` refuses a player before the engine admits them, with a reason they see.
- `volt list` also names each refused plugin with its reason.
- `plugin.json` takes `$schema`, `website`, `license` and a starting `logLevel`; `templates/plugin.schema.json`
  lets editors complete it.
- `Messages::BroadcastKey` sends a key to every player in their own language.
- `Args::Target` reads through `->`, and `Args::Opt::ValueOr` returns the value or a fallback.
- `Options::Reload()` reads the settings file again.

## 1.7.0 (2026-09-26)

### Breaking

- Put `Game csgo/addons/voltmod` directly above `Game csgo` in `gameinfo.gi` (`voltmod serve` and
  `voltmod run` add it) and delete `addons/metamod/voltmod.vdf`: the engine now loads the framework
  as `server_valve`, Metamod is optional, and every plugin must be rebuilt for host ABI 4.
- Call `IHost::EngineInterface`, `ServerInterface` or `BaseDir` where you called `IHost::Metamod()`.
- CMake 4.4 is required. `VOLTMOD_DISABLE_PCH` is gone; set `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON` instead.
- HTTP: send every request through `Http.Send`; the `Get`/`Post`/`Put`/`Patch`/`Delete` helpers
  are gone, and `HttpRequest::Headers` is a name-to-value map (`AddHeader` is gone).
- Reference plugin images as `s2r://panorama/images/custom_game/<set>/<name>.vtex`, so the
  Workshop addon packs them.

### New

- Hide an entity from a whole team with `HideFromTeam`, and let a trace pass through every part of
  a prop with `TraceOptions::IgnoreOwnedBy`.

### Fixed

- Custom screens show their state again after a client finishes loading.

## 1.6.0 (2026-09-25)

### Breaking

- Rebuild every plugin against this release: the host ABI is now 3.
- Call entity verbs on the entity or pawn (`entity.Remove()`, `AcceptInput`, `pawn.GiveItem`,
  `StripWeapons`) and spawn with `runtime.Entities.Spawn`; `EntityOps`, `Items` and `PawnOps` are gone.
- Getters name what they return (`Entities.Pawn(slot)`, `Entities.Get(ref)`, `player.Controller()`),
  and `runtime.World.Trace`, `Rounds`, `Precache` and `NetChannels` now sit directly on `runtime`.
- Move `configs/translations` and `configs/migrations` to `translations/` and `migrations/`, and
  call `LoadConfig` where you called `LoadStandardConfig`.
- Panorama screens extend `screen.xml.j2`, and image sets move from `images/custom_game/<set>/`
  to `images/<set>/`.

### New

- Hook and apply entity damage, spawn props, particles and beams, and find entities with
  `FindAll` and `AlivePawns`.
- Built against CS2 build 2000917 and hl2sdk-cs2 2026.09.25.

## 1.5.5 (2026-09-20)

### Breaking

- `GameEvents::FireEvent` takes `broadcast` where it took `dontBroadcast`; invert the second
  argument at every call site.

### Fixed

- Clients accept F1/F2 on a plugin vote and their ballots reach the plugin.
- Players the engine brings back after a map change reconnect, so a stale player no longer
  holds a slot and the occupied-slot count stays right.
- The map change message names the downloaded addon, so clients keep it mounted across the change.

## 1.5.4 (2026-09-20)

### New

- `voltmod build --install-all` copies every plugin to the server instead of one named plugin.
- The Panorama menu ships a default image asset, so a screen that sets no image still renders one.

### Fixed

- Opening a Panorama menu draws its home page only for the session that asked for it, instead of
  for everyone holding the menu open.

## 1.5.3 (2026-09-19)

### New

- `voltmod database tables` folds `ALTER TABLE ... ADD COLUMN` into the generated table spec.

### Fixed

- Hiding a Panorama menu removes its `custom_hud_layout` entity, so a second menu (or another
  plugin's) renders instead of showing only the cursor.

## 1.5.2 (2026-09-19)

### Breaking

- Rebuild every plugin together with the host: `HostAbiVersion` is 2.
- Chat commands take only the `!` prefix; `.ban` is plain chat now.
- Run `voltmod lint [path]` in place of `voltmod modgraph --plugins <path>`.
- Use `ServiceExchange::Publish<T>`, `Unpublish<T>` and `Get<T>` in place of `PublishNamed`,
  `UnpublishNamed` and `Find`; pass a key when one interface has several providers.
- `Translations::SetPlayerLanguage` now sets the language for every plugin; call it with `""`
  where you called `ClearPlayerLanguage`.
- Read `.Label` and `.Value` where you read `.first` and `.second` on `ChoiceRow::Choices`,
  `DurationMenu::Presets`, the `Flow` step lists and `ChatColors::PaletteChoices`; brace lists
  such as `{{"1 HP", 1}}` still compile.
- Re-render your Panorama screens and republish their addon with the server build: classes are
  BEM kebab-case (`row__label`) and `IconNames` is `IconSetNames`.

### New

- The `menu` Panorama block and `PanoramaMenuLayout` draw a whole menu screen, with an optional
  home page; see the Panorama guide.
- `ConVars::ExecuteClientCommand(slot, command)` runs a console command as a player.
- `voltmod database tables` folds `ALTER TABLE ... DROP COLUMN` into the generated table spec.

### Fixed

- A chat command reaches its plugin even when another plugin's `OnPlayerChat` consumes the line.
- `voltmod panorama check` no longer reads a number in a top-level `@define` as a class name.

## 1.5.1 (2026-09-18)

### Breaking

- Write `DATABASE` instead of `FEATURES DATABASE` in `voltmod_add_plugin` and `voltmod_add_tests`.
- Link `VoltMod::Portable` where you linked `VoltMod::Headers`; tested SDK-free code can now call
  `Log`, `Time` and `Strings`.
- Rebuild every plugin together with the host: plugins built against 1.5.0 cannot load.
- `runtime.Version` and `volt list` show the `plugin.json` version without a commit suffix.

## 1.5.0 (2026-09-18)

### Breaking

- The framework runs as one host per server process. `addons/metamod/voltmod.vdf` is the only
  Metamod plugin left; your plugin is a module under `addons/voltmod/plugins/<name>/` that the host loads.
  Its binary sits beside `plugin.json`; per-plugin `bin/` directories are gone.
- Derive your load-cycle class from `VoltMod::Plugin`, construct the base from `Runtime&`, and
  override `Load()`. `VOLTMOD_PLUGIN(MyPlugin)` constructs it after the runtime starts and
  destroys it before the runtime stops. The macro comes from `<VoltMod/App/PluginEntry.hpp>`;
  include it in that one .cpp.
- Describe the plugin in a hand-written `plugin.json` beside its `CMakeLists.txt`: `name`,
  `version`, `logTag`, `description`, `author`, `dependencies`, `optionalDependencies`.
  `voltmod_add_plugin(<name>)` reads it and takes no `VERSION`. `PluginInfo`, `Plugin::Info()`
  and `WithBuildInfo` are gone; read `runtime.PluginName` and `runtime.Version` instead.
- `LoadStandardConfig(runtime, config)` and `runtime.PluginFile(relative)` know the plugin's
  directory; `StandardLoadOptions::Addon` is gone.
- Link `VoltMod::Sdk` where you linked `VoltMod::Runtime`. There is no alias for the old name.
- `voltmod_add_plugin` no longer writes a per-plugin `.vdf`. Delete the old ones from
  `addons/metamod/` on servers deployed before this release.
- `Core` headers now sit in `Signals/`, `Text/`, `Slots/`, `Time/` and `Files/`, `EffectManager`
  is in `Players/`, the config headers are in `App/Config/` and `Engine/MetamodGlobals.hpp` is
  `Engine/Detours.hpp`. `<VoltMod/Api.hpp>` and `<VoltMod/App/Config.hpp>` keep their spelling.
- Bind gamedata with `Bindings::Bind(lookup)` instead of `Bindings::Load(path)`; the host reads
  and resolves the file and hands every plugin the same lookup.
- The host no longer orders plugins by their dependency lists: it loads alphabetically. Resolve
  another plugin through `runtime.Exchange` when you use it rather than caching it in `Load`.
  `dependencies` still refuses a plugin whose entry is missing or refused, and still decides what
  `volt unload` blocks and `volt reload` takes down; `optionalDependencies` refuses nothing.
- Declare settings as `VoltMod::Options<Settings>` where you used `JsonConfig`; a failed reload
  keeps the previous settings. See `docs/config.md`.
- Rename the calls that said `Start`: `Database::Connect`, `Runtime::Initialize`,
  `MenuSurface::OpenSession` and `Flow::Begin`.
- Build the host and your plugins from one build and deploy them together. The host refuses a
  plugin whose ABI version is not its own, and one built against a different schema layout.

### New

- `volt list`, `volt status [name]`, `volt load`, `volt unload`, `volt reload` and
  `volt log <name> <level>` drive plugins from the server console.
- Log output, gamedata resolution and schema verification each happen once per server rather than
  once per plugin, so a broken signature after a game update is reported once.
- `Logger<T>` prefixes each log line with the name of `T`, beside the plugin's tag.

## 1.4.7 (2026-09-16)

### Breaking

- Gamedata loads in one call: `Bindings::Load(path)` replaces `GameData`, and anything it could
  not bind is listed in `Bindings::Failures`.
- `gamedata.jsonc` renames its sections: `signatures` is `functions`, `addresses` is `globals`,
  and an entry's `library` is `module`. The schema beside the file flags what you miss.
- Hook a virtual function with `HookVirtual` and call it straight off its `VirtualFn`, which now
  carries its own class table. `ClassSlot` and `HookClassSlot` are gone.
- Ask a service whether it works instead of checking a capability: `Hooks.Movement`,
  `Hooks.Teleport`, `Hooks.Visibility`, `Hooks.ClientConVars` and `Screens` each answer
  `Available()`, and the error says why not.
- `runtime.LoadReport` is `runtime.LoadSteps`; run work through `Optional` and `Required` instead
  of `Run` and `Require`.
- `OriginalVfn` is `OriginalSlotLookup`.

### New

- A gamedata entry can name a base class instead of a per-platform number, and VoltMod finds it
  through RTTI.
- `gamedata.jsonc` records the server build it was verified on, and the load warns when the
  running server is a different one.
- Every load writes `addons/voltmod/gamedata/resolved.<platform>.json`. Keep one from a working
  build to compare against after a CS2 update.
- Entity lookups switch off with a reason when the gamedata offset stops pointing at the entity
  system, instead of reading whatever is there.

## 1.4.6 (2026-09-14)

### Breaking

- `runtime.Ui`, `UiPanel`, `UiPanels`, `UiClick` and the writer types are gone. Draw with
  `runtime.Screens` and `Screen`; presses arrive as `ButtonPress`. `Capability::UiClicks` is
  `Capability::ButtonPresses`.
- `runtime.Menus` is now a `MenuRouter` and center HTML is `runtime.CenterHtml`. Start a menu with
  `Menus.Start(slot, menu, options)` instead of `Open(slot, menu, options)`. A custom
  `MenuSurface` must implement `Start` and `IsOpen`.
- Remove `voltmod/*:with_database` from consumer recipes. The Database library always ships;
  plugins still opt in with `FEATURES DATABASE`.
- `voltmod panorama preview` is gone.

### New

- `PanoramaMenu` draws menus on a plugin's own `MenuLayout`, with clicks and sidebar tabs. Hand it
  to `runtime.Menus.Prefer`; players who cannot see the layout get center HTML.
- `PlayerScreens` creates a layout's screen for a player the first time something draws for them.

## 1.4.5 (2026-09-14)

### Breaking

- `HookVTable` is now `HookClassSlot`. Its binding `VHookBinding` is `ClassSlot`, whose `Method` is
  `Function`, and `VFn` is `VirtualFn`.
- The `Hooked*` stand-in types are `Engine*`: `EnginePawn`, `EngineClient`, `EngineMovementServices`.
- An after-handler no longer receives the return value; it takes the same arguments as the
  before-handler. `HookClassSlot` has no live-instance parameter.
- Gamedata keys are named after the engine symbol, so `RunCommand` is
  `CPlayer_MovementServices::RunCommand`. Rename the keys in a custom `gamedata.jsonc`.

### New

- `HookFunction` hooks a signature-bound function where its code starts, for a function no class
  vtable reaches.

### Fixed

- Clients now mount the workshop addons `Addons` requires, not only download them.

## 1.4.4 (2026-09-14)

### Breaking

- Workshop `Addons` members are renamed: `Pending`, `HasPending` and `Ready` are now
  `Missing`, `HasMissing` and `Downloaded`.
- `voltmod panorama publish` is gone. Build a workshop addon with
  `voltmod panorama compile --addon NAME --no-deploy`.
- `Schema/Layout.hpp` and `Schema/Notify.hpp` are no longer public; include
  `<VoltMod/Schema/Api.hpp>`.

### Fixed

- Setting a field the engine does not network no longer sends a network update.

## 1.4.3 (2026-09-14)

### Fixed

- Plugins load on Linux servers. Windows and Linux lay entity classes out differently, so each
  has its own schema offsets; regenerate the Linux ones with
  `voltmod schemagen --platform linux --dump <server.json>`.
- Vtable hooks resolve on Linux, where the game's libraries hide their vtable symbols.
- The Linux patterns for `CEntityInstance_AcceptInput` and `CustomHudSetHasClass` match
  build 2000908.

## 1.4.2 (2026-09-13)

### Breaking

- Database `Run` now blocks and is for plugin load only; use `RunAsync` everywhere
  else. Failed jobs return a `Result`.
- Migrations use one folder for every database, as `0001_name.sql`.
- Panorama stylesheets are `.css.j2` files, and the accent options are gone.
- `voltmod package` is now `uv run poe release`.

### New

- Menu rows can show icons.

### Fixed

- The toast border is back.

## 1.4.1 (2026-09-12)

### Fixed

- Linux builds with gcc-14, including the database clients.

## 1.4.0 (2026-09-12)

### Breaking

- The database module supports Postgres, MariaDB and SQLite. The Conan option
  `with_postgres` is now `with_database`.
- Hooks run on KHook, so servers need a Metamod build that ships it.
- Menu, Panorama and engine helper types were renamed. The guides in `docs/` use the
  new names.

### New

- `voltmod gamedata` finds and repairs signatures a CS2 update broke.
- Panorama screens are built from Jinja templates, with `voltmod panorama check` and a
  browser preview.
- Commands can target several players at once.

### Fixed

- A failed migration is no longer marked as applied.
