# Testing {#testing_guide}

[TOC]

Unit tests use [doctest](https://github.com/doctest/doctest) and stay SDK-free: no Metamod, no
HL2SDK, no live `Runtime`, entity or database connection. Test logic that takes plain values and
returns plain values - parsers, the target-selector grammar, angle math, decaying scores,
throttles, detector heuristics. Keep that logic in free functions over structs and the rest
follows.

## Running

```bash
uv run poe test                       # build, then ctest
uv run poe test -R SteamId            # only matching case names
ctest --preset windows-msvc-release --output-on-failure
ctest --preset windows-msvc-release -N            # list without running
```

Each test case is its own CTest entry, so a CI report names the failing case. The presets set
`noTestsAction: error`, so a run that discovers nothing fails the job instead of passing.

Run the binary under `build/<preset>/` directly for doctest's own filters:

```bash
voltmod-tests --list-test-cases
voltmod-tests --test-case="SteamId::*"
voltmod-tests --source-file="*Targeting*"
voltmod-tests --success            # print passing asserts too
```

`conan create` excludes `tests/`, so a package build compiles no tests. CI builds the source checkout
separately:

```yaml
- run: voltmod build linux-steamrt-release --no-lockfile
- run: voltmod test linux-steamrt-release
```

`--no-lockfile` resolves without `conan.lock`, which CI needs because it builds against SDK
packages it just created from the HEAD recipes.

## Writing a case

Put cases in `tests/<Module>/*.cpp`. `voltmod_add_tests()` supplies `main` and picks up new files.

```cpp
#include <VoltMod/Core/Text/Strings.hpp>
#include <doctest/doctest.h>

using VoltMod::ParseDuration;

TEST_CASE("ParseDuration: suffixes")
{
    CHECK_EQ(ParseDuration("30"), 30);
    CHECK_EQ(ParseDuration("5m"), 300);
    CHECK_EQ(ParseDuration("perm"), 0);   // permanent
    CHECK_EQ(ParseDuration("nope"), -1);  // unparseable
}
```

`CHECK*` records a failure and continues; `REQUIRE*` stops the case, so use it before
dereferencing or indexing a value under test:

```cpp
auto snap = Detectors::AimSnap::FindSettledSnap(window, cfg);
REQUIRE(snap.has_value());     // stop here rather than crash on snap->Ago below
CHECK_EQ(snap->Ago, 1);
```

Compare directly. `CHECK_EQ` and friends print both operands; wrapping the comparison in a
predicate loses that:

```text
ParseDurationTests.cpp(9): ERROR: CHECK_EQ( ParseDuration("5m"), 300 ) is NOT correct!
  values: CHECK_EQ( 5, 300 )
```

`doctest::Approx`'s tolerance is *relative*: `|a - b| < epsilon * (scale + max(|a|, |b|))`, so
`Approx(180.0f).epsilon(0.01)` accepts a 1.81 gap. Where the test means an absolute tolerance
(degrees, score units), write a local `Near(a, b, eps)` helper; the angle and decaying-score
suites do. For throws use `CHECK_THROWS_AS`, `CHECK_THROWS_WITH` or `CHECK_NOTHROW`.

Each `SUBCASE` re-runs the enclosing body from the top, so setup is written once and every branch
gets a fresh copy with no fixture class:

```cpp
TEST_CASE("FilterRoster: team selectors")
{
    auto roster = Roster();                 // rebuilt for every SUBCASE below

    SUBCASE("@ct matches both CTs")
    {
        auto r = FilterRoster(roster, ParseTargetToken("@ct"), {.AllowMultiple = true}, Caller);
        CHECK_EQ(Size(r), std::size_t{2});
    }
    SUBCASE("immunity narrows instead of failing")
    {
        roster[1].Targetable = false;
        auto r = FilterRoster(roster, ParseTargetToken("@ct"), {.AllowMultiple = true}, Caller);
        CHECK_EQ(FrontSlot(r), 2);
    }
}
```

`TEST_CASE_TEMPLATE` instantiates the body once per type, reporting each as its own case:

```cpp
TEST_CASE_TEMPLATE("Trim accepts any string-like input", T, const char*, std::string)
{
    CHECK_EQ(Strings::Trim(T{"  hi  "}), std::string("hi"));
}
```

## Case names cannot contain `[`, `]` or `;`

Discovery runs the freshly built binary with `--list-test-cases` and parses the output as a CMake
list, where `[`...`]` groups and `;` separates. An unmatched bracket folds every following case
into one entry and fails configure with the unrelated-looking `add_test called with incorrect
number of arguments`; a semicolon silently splits one case into two bogus entries.
`voltmod_add_tests()` scans the sources and fails configure naming the offending file instead.

Spell interval bounds out - `wraps to -180 exclusive through 180 inclusive`, not
`wraps into (-180, 180]`. Parentheses, commas, colons, `<`, `>` and `::` are fine.

## Adding tests to a plugin

```cmake
voltmod_add_tests(myplugin-tests
    SOURCES
        src/Detectors/AimSnapCore.cpp
    DEFINITIONS
        MYPLUGIN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)
```

`voltmod_add_tests(<name> [DATABASE] [SOURCES ...] [DEFINITIONS ...])` comes from
`cmake/VoltModTests.cmake`, a build module of the Conan package. It globs `tests/**/*.cpp`
(excluding `tests/Api/`), supplies doctest's `main`, links `doctest::doctest` and
`VoltMod::Portable` (the framework code that builds without the game SDK), adds the plugin's
`src/` and `tests/` to the include path, and registers the cases with CTest. It is a no-op when
`BUILD_TESTING` is off.

| Argument | Means |
| --- | --- |
| `SOURCES` | the plugin's SDK-free translation units to compile beside the test cases |
| `DATABASE` | also link `VoltMod::Database`, so a test can open a SQLite database and run the plugin's migrations |
| `DEFINITIONS` | compile definitions for the test target |

Test binaries never link the plugin module or `VoltMod::Sdk`, so nothing drags in Metamod. The
Conan side is one line in `conanfile.py`:

```python
def build_requirements(self):
    self.test_requires("doctest/2.5.2")
```

## Api surface checks

Each `Api.hpp` aggregate must compile as the only VoltMod include in a translation unit, and
`RootApiSurfaceTest.cpp` checks that the main umbrella pulls in neither the JSON layer nor the
menu-building surface. These are compile-only, and they need the full HL2SDK and Metamod build, so
they live in `tests/Api/` and compile into `voltmod-api-surface-check` - an object library in the
root `CMakeLists.txt` linked against `VoltMod::Sdk` and `VoltMod::Database`.

## Module layering and source conventions

```sh
uv run poe modgraph                 # the framework's own module layering
voltmod lint                        # a consumer repo's plugins/ sources
```

`modgraph` checks the framework's module dependencies and its source conventions. `lint [path]`
checks the source conventions under a consumer's `plugins/`, not framework layering; it defaults to
the working directory.
