<!-- markdownlint-configure-file { "MD024": { "siblings_only": true } } -->

# Changelog

What changed in each VoltMod release. Older history is in git.

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
