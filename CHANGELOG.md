# Changelog

Notable changes to the VoltMod Conan package, newest first. Versions follow the
`v*` tags that publish the package. Releases before 1.4.0 are in the git history.

## [1.4.1] - 2026-09-12

### Fixed

- The framework compiles with gcc-14: service types that GCC reads as changing
  meaning are qualified.
- The Linux build pins the tool requirements the database clients need and the
  sqlpp23 revision with its connectors enabled.
- The package resolves against the published SDK revisions.

## [1.4.0] - 2026-09-12

### Breaking

- The database module runs on sqlpp23 with Postgres, MariaDB and SQLite drivers.
  `PostgresDatabase`, `Query`, `Exec` and `WithConnection` are gone, and the Conan
  option `with_postgres` is `with_database`.
- Engine hooks install through KHook instead of SourceHook. Handlers take the hooked
  object first, and the installers in `Unsafe/Hook.hpp` replace `VOLTMOD_VHOOK` and
  `SH_DECL_HOOK` and return a `Subscription`. Servers need a Metamod build with KHook.
- Engine headers moved into `Memory/`, `GameData/`, `ConVars/`, `Net/` and `Server/`.
- vtable slots are found by signature. Delete the `messages` section from
  `gamedata.jsonc`; `Bindings::CustomHudClicked` is gone.
- JSON configuration uses Glaze reflection instead of nlohmann.
- Schema fields come from a generated accessor layer that is verified at load.
- Menus: `MenuManager` is `CenterHtmlMenu`, `MenuSession` is `MenuSurface`, a shared
  `MenuStack` holds the open menus, and `runtime.Freeze.Enable` replaces
  `runtime.Menus.FreezeWhileOpen`.
- Panorama screens render from Jinja templates into the build tree, their bindings
  come from the layout, and the YAML theme is gone.
- Movement events are `Before`, `After` and `Rewrite`, carrying `PlayerInput`.
- `Transmit` is `Visibility` (`ShowOnlyTo`, `ShowToEveryone`), `ClientCvars` is
  `ClientConVars`, and `SharedSource` is `SharedLifecycle`.
- The targeting policy answers with SteamIDs.

### Added

- `voltmod gamedata` checks and repairs drifted signatures, and the framework dumps
  the schema itself.
- `voltmod panorama check` validates screens and a browser preview renders them;
  dialog, listrow, tabs, pager and button blocks.
- `Args::Targets` binds multi-target selectors.
- `HookResult`, a hook verdict any header can name.

### Fixed

- An unreadable migration is no longer recorded as applied.
- Convar values written to the console are quoted.
- Translations try the active language before falling back to English.
- Stopping the HTTP client cancels in-flight requests.
