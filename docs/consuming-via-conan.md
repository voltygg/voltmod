# Consuming VoltMod with Conan {#conan_guide}

[TOC]

VoltMod is a Conan package. Do not add the framework as a Git submodule or with
`add_subdirectory`. The package brings VoltMod, HL2SDK, Metamod, the generated protobuf sources
and the CMake functions below. `voltmod init` sets all of this up; this page is for adding it to
an existing project and for what the functions take.

The remote is public - no login or token.

## Add the package

In the consumer's `conanfile.py`:

```python
requires = ("voltmod/[~1.5]",)
```

Install the profiles and the remote once:

```sh
conan config install https://github.com/voltygg/voltmod.git -sf conan
```

The profiles are not optional. Linux binaries are built with gcc-14 against the old libstdc++ ABI
(`_GLIBCXX_USE_CXX11_ABI=0`, which is what the Valve prebuilt libs use) and Windows binaries with
the static MSVC runtime. The recipe's `validate()` rejects a mismatching profile and points back
here.

In the root `CMakeLists.txt`:

```cmake
find_package(voltmod CONFIG REQUIRED)
add_subdirectory(plugins/my-plugin)
```

That generates the VoltMod targets, links `VoltMod::HL2SDK` and `VoltMod::Metamod` behind them,
and includes `cmake/VoltModPlugin.cmake` and `cmake/VoltModTests.cmake`.

## Packages and targets

| Package | Contents |
| --- | --- |
| `voltmod/x.y.z` | The host binary, the Sdk and Database libraries, headers, CMake helpers, gamedata, templates |
| `hl2sdk-cs2/<yyyy.mm.dd>` | Trimmed HL2SDK in mirror layout: headers, prebuilt Valve libs, generated `.pb.h`/`.pb.cc`, and the source-only TUs each plugin compiles itself. Versioned by the upstream commit date |
| `metamod-source/2.0.0.<yyyymmdd>` | Metamod core and KHook headers, header-only |
| `sqlpp23/<x.yy>` | sqlpp23 headers and `sqlpp23-ddl2cpp`. The `with_postgresql`, `with_mariadb` and `with_sqlite3` options add the matching connector |

| Target | Is |
| --- | --- |
| `VoltMod::Sdk` | The framework library a plugin links. `voltmod_add_plugin` links it for you |
| `VoltMod::Database` | Postgres/MariaDB/SQLite plus sqlpp23, added by `FEATURES DATABASE` |
| `VoltMod::Headers` | The include directory with glaze and magic_enum behind it, and no SDK. What a test binary links |
| `VoltMod::VoltMod` | Every library component at once |

The package also carries the built host as the `host` install component, which nothing links.
`cmake --install <build> --component host --prefix dist` stages `dist/addons/voltmod/` and
`dist/addons/metamod/voltmod.vdf` for a server; `voltmod install` does it as part of an install.

Source modules are architecture boundaries, not Conan components.

The hl2sdk package ships `hl2sdk-sources.cmake`, which publishes the one thing usage requirements
cannot express: the SDK translation units a consumer must compile itself.
`hl2sdk_attach_plugin_support(<target>)` attaches the per-module `convar.cpp` and
`memoverride.cpp` with the warning, PCH and unity exclusions they need, and `voltmod_add_plugin`
calls it. Everything else is ordinary `package_info()`.

Third-party dependencies (cpr, glaze, libpq, mariadb-connector-c, sqlite3, openssl) come from
Conan Center and compile locally: no binaries exist for the gcc-14 old-ABI profile the Valve libs
require. That costs each machine once, and CI caches the Conan home. CI publishes Linux Release
binaries for VoltMod and the SDK packages; Windows and Debug builds compile what is missing
through `--build=missing`.

## CMake functions

```cmake
voltmod_add_plugin(<name> [SOURCES <file>...] [FEATURES DATABASE])
```

Builds the library the host loads and installs it as the `<name>` component. `SOURCES` defaults
to a recursive glob of `src/*.cpp`. `FEATURES DATABASE` links `VoltMod::Database` and puts the
sqlpp23 connector headers ahead of the framework's in the precompiled header.

The name must be the CMake target, the plugin's directory and the `name` in the `plugin.json`
beside that `CMakeLists.txt`. The function reads `name` and `version` from the manifest and fails
at configure time when either is wrong or missing - the host would refuse the plugin later for
the same reason. There is no `VERSION`, `DEPENDS` or `OPTIONAL_DEPENDS` argument: versions and
dependencies live in `plugin.json` (see @ref plugin_guide).

```cmake
voltmod_add_tests(<name> [SOURCES <file>...] [FEATURES DATABASE] [DEFINITIONS <define>...])
```

Builds a doctest binary from a recursive glob of `tests/*.cpp` plus the SDK-free `SOURCES`
recompiled beside them, links `VoltMod::Headers`, and registers the cases with CTest. It is a
no-op when `BUILD_TESTING` is off. `FEATURES DATABASE` also links `VoltMod::Database`, so a test
can open a SQLite database and run the plugin's migrations. A `TEST_CASE` name may not contain
`[`, `]` or `;`, which CTest discovery cannot round-trip; the function fails on one that does.
See @ref testing_guide.

## Working on the framework and a plugin together

Register the checkout as the editable `voltmod` package once:

```sh
conan editable add <path-to-voltmod>
```

`voltmod build` then compiles the checkout into its own `build/<preset>` before the plugins, so
both sides stay incremental and the plugins link the checkout in place.

Before committing, build against a real package the way CI will:

```sh
voltmod build --relock
```

That exports the checkout to the local cache with `conan export-pkg`, re-pins `voltmod` in
`conan.lock`, drops the editable registration, and fails if the plugin build tree was not
reconfigured against the new package. Commit the relocked `conan.lock` with the plugin change.

`voltmod build --no-lockfile` resolves without `conan.lock`, which is what an SDK bump needs.

## Publishing (maintainers)

Everything publishes from this repo through `uv run poe release` (`tools/release`).

- VoltMod goes out from `.github/workflows/release.yml` on every `v*` tag, which must match the
  version in `conanfile.py`. It uploads Linux Release, then creates the GitHub release from the
  `CHANGELOG.md` entry.
- The SDK packages publish Linux binaries from `recipes/` through `.github/workflows/sdk.yml`, on
  a push to `main` that touches them. A daily job watches both upstreams and opens a PR when a
  branch tip moves; that PR builds the new SDK and the framework against it, so a version that
  cannot compile the framework never publishes.

Both need `CLOUDSMITH_USERNAME` and `CLOUDSMITH_API_KEY` with write access; entitlement tokens are
read-only. If the remote is unreachable:

```sh
uv run poe release build sdk          # conan create the SDK recipes
uv run poe release build framework    # then the framework against them
```
