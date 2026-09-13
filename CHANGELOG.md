# Changelog

What changed in each VoltMod release. Older history is in git.

## 1.4.1 (2026-09-12)

- **Fixed:** Linux builds with gcc-14, including the database clients.

## 1.4.0 (2026-09-12)

- **Breaking:** The database module supports Postgres, MariaDB and SQLite. The Conan
  option `with_postgres` is now `with_database`.
- **Breaking:** Hooks run on KHook, so servers need a Metamod build that ships it.
- **Breaking:** Menu, Panorama and engine helper types were renamed. The guides in
  `docs/` use the new names.
- **New:** `voltmod gamedata` finds and repairs signatures a CS2 update broke.
- **New:** Panorama screens are built from Jinja templates, with
  `voltmod panorama check` and a browser preview.
- **New:** Commands can target several players at once.
- **Fixed:** A failed migration is no longer marked as applied.
