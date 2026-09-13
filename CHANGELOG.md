<!-- markdownlint-configure-file { "MD024": { "siblings_only": true } } -->

# Changelog

What changed in each VoltMod release. Older history is in git.

## 1.4.2 (2026-09-13)

### Breaking

- Database `Run` now blocks and is for plugin load only; use `RunAsync` everywhere
  else. Failed jobs return a `Result`.
- Migrations use one folder for every database, as `0001_name.sql`.
- Panorama stylesheets are `.css.j2` files, and the accent options are gone.
- `voltmod package` is now `uv run poe release`.
- Only the database-enabled package is prebuilt; building with `with_database=False`
  compiles VoltMod from source.

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
