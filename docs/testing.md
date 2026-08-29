# Testing {#testing_guide}

[TOC]

Unit tests use [doctest](https://github.com/doctest/doctest) and remain SDK-free.
They cover logic such as parsing, targeting, and score calculations without
loading Metamod or HL2SDK.

## Running

```bash
uv run poe test               # configure -> build -> ctest
ctest --preset windows-msvc-release   # tests only
ctest --preset windows-msvc-release --output-on-failure
```

Each test case is its own CTest entry, so `ctest -R` can filter it and CI failure
reports identify the exact case:

```bash
ctest --preset windows-msvc-release -R "SteamId"
ctest --preset windows-msvc-release -N          # list without running
```

## In CI

`conan create` does not include `tests/` and disables `BUILD_TESTING`. CI builds
the source checkout separately and runs the Linux preset:

```yaml
- run: voltmod build linux-steamrt-release --no-lockfile
- run: voltmod test linux-steamrt-release
```

`--no-lockfile` resolves without `conan.lock`, which CI needs because it builds
against SDK packages it just created from the HEAD recipes. The test presets set
`noTestsAction: error`, so a run that discovers no cases fails the job instead of
reporting success.

Run the test binary directly to use doctest's case and source filters:

```bash
build/windows-msvc-release/vendor/voltmod/voltmod-utils-tests.exe --list-test-cases
build/windows-msvc-release/vendor/voltmod/voltmod-utils-tests.exe --test-case="SteamId::*"
build/windows-msvc-release/vendor/voltmod/voltmod-utils-tests.exe --source-file="*Targeting*"
build/windows-msvc-release/vendor/voltmod/voltmod-utils-tests.exe --success   # print passing asserts too
```

## Writing a test

Put test cases in `tests/<Module>/*.cpp`; `voltmod_add_tests()` supplies `main`
and discovers new files automatically. `tests/Api/` is excluded because those
compile-only checks need the full HL2SDK and Metamod build.

```cpp
#include <VoltMod/Core/Strings.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::ParseDuration;

TEST_CASE("ParseDuration: suffixes")
{
    CHECK_EQ(ParseDuration("30"), 30);
    CHECK_EQ(ParseDuration("5m"), 300);
    CHECK_EQ(ParseDuration("perm"), 0);   // permanent
    CHECK_EQ(ParseDuration("nope"), -1);  // unparseable
}
```

### Assertions

`CHECK*` records a failure and continues. `REQUIRE*` stops the case, so use it
before dereferencing or indexing a value under test:

```cpp
TEST_CASE("FindSettledSnap picks the snap nearest the shot")
{
    auto snap = Detectors::AimSnap::FindSettledSnap(window, cfg);
    REQUIRE(snap.has_value());     // stop here rather than crash on snap->Ago below
    CHECK_EQ(snap->Ago, 1);
}
```

Direct comparisons print operand values. Binary forms such as `CHECK_EQ` and
`CHECK_LT` provide the same decomposition:

```text
TEST CASE:  ParseDuration: suffixes

ParseDurationTests.cpp(9): ERROR: CHECK_EQ( ParseDuration("5m"), 300 ) is NOT correct!
  values: CHECK_EQ( 5, 300 )
```

Wrapping a comparison in a predicate loses that detail, so compare directly
when possible.

`doctest::Approx` is the built-in float comparison, but note its tolerance is *relative*:
`|a - b| < epsilon * (scale + max(|a|, |b|))`, so `Approx(180.0f).epsilon(0.01)` accepts a
1.81 gap, not 0.01. Where an absolute tolerance is what the test means (degrees, score
units), a small local `Near(a, b, eps)` helper is the honest choice; the angle and
decaying-score suites use one deliberately.

For expected throws use `CHECK_THROWS_AS(expr, Type)`, `CHECK_THROWS_WITH`, or
`CHECK_NOTHROW`.

### Sharing setup with SUBCASE

Each `SUBCASE` re-runs the enclosing case body from the top, so setup is written once and
every branch gets a fresh copy, with no fixture class and no leakage between branches:

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

### Same assertions over several types

`TEST_CASE_TEMPLATE` instantiates the body once per type in the list, reporting each as its
own case:

```cpp
TEST_CASE_TEMPLATE("Trim accepts any string-like input", T, const char*, std::string)
{
    CHECK_EQ(Strings::Trim(T{"  hi  "}), std::string("hi"));
}
```

## Adding tests to a plugin

`voltmod_add_tests()` (from `cmake/VoltModTests.cmake`, included by the framework's root CMakeLists) owns
the wiring: it globs `tests/**/*.cpp`, supplies doctest's `main`, links `doctest::doctest`,
adds the framework's include dir, and registers the cases with CTest. `SOURCES` is the list of
SDK-free TUs to recompile. Test binaries never link the plugin module or the framework, so
nothing drags in Metamod.

```cmake
if(BUILD_TESTING)
    voltmod_add_tests(myplugin-tests
        SOURCES
            src/Detectors/AimSnapCore.cpp
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        LIBRARIES nlohmann_json::nlohmann_json
    )
endif()
```

The Conan side is one line in `conanfile.py`:

```python
def build_requirements(self):
    self.test_requires("doctest/2.5.2")
```

## Api surface checks

Each `Api.hpp` aggregate must compile as the only VoltMod include in a
translation unit. `RootApiSurfaceTest.cpp` also verifies that the main umbrella
does not include nlohmann or the menu-building surface. These files are
compile-only tests.

They compile into `voltmod-api-surface-check`, an object library defined in the root
`CMakeLists.txt` and linked against `VoltMod::Runtime` (and `VoltMod::Database` when
`VOLTMOD_ENABLE_POSTGRES` is on) so they see the same include paths and generated HL2SDK headers
the framework itself needs - that's also why they live outside `voltmod-utils-tests` and are
excluded from its `tests/**/*.cpp` glob.

## Checking module layering and source conventions

```sh
uv run poe modgraph                 # the framework's own module layering
voltmod modgraph --plugins .        # a consumer repo's plugins/ sources
```

Without `--plugins`, `modgraph` checks the framework's module dependencies and
source conventions. `--plugins <path>` checks consumer source conventions but
not framework layering. Run it from the consumer repository root.

## What is worth testing

Test logic that takes plain values and returns plain values: parsers and formatters, the
target-selector grammar, angle math, decaying scores, throttles, migration-version
extraction, detector heuristics, threshold reachability. Anything that needs a
live `Runtime`, an entity, or a database connection is out of scope for this
suite. Keep that logic thin and push the decisions into free functions over
structs; that is what makes the rest testable.

## Naming rule: no `[`, `]` or `;`

Discovery registers the CTest entries by running the freshly built binary with
`--list-test-cases` and parsing the output as a CMake list, where `[`...`]` groups and `;`
separates. So an unmatched bracket folds every following case into one entry (configure then
dies with the unrelated-looking `add_test called with incorrect number of arguments`), and a
semicolon silently splits one case into two bogus entries.

`voltmod_add_tests()` scans the test sources and fails configure with the offending file rather
than letting either happen. Spell interval bounds out (`wraps to -180 exclusive through 180
inclusive`, not `wraps into (-180, 180]`). Parentheses, commas, colons, `<`, `>` and `::`
are all fine.
