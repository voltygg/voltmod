# Testing {#testing_guide}

[TOC]

Unit tests use [doctest](https://github.com/doctest/doctest), pulled in as a Conan
`test_requires` and linked as `doctest::doctest`. Tests are SDK-free: they exercise pure
logic - parsers, math, score decay, targeting rules - without loading Metamod or the
HL2SDK, so the whole suite links in seconds and runs in milliseconds.

## Running

```bash
uv run poe build                      # configure -> build -> ctest
ctest --preset windows-msvc-release   # tests only
ctest --preset windows-msvc-release --output-on-failure
```

Every test case is its own ctest entry, so `ctest -R` filters and CI failure reports name
the exact case:

```bash
ctest --preset windows-msvc-release -R "SteamId"
ctest --preset windows-msvc-release -N          # list without running
```

To iterate on one case, running the binary directly is faster and doctest's CLI is richer
than ctest's:

```bash
build/windows-msvc-release/vendor/cs2-kit/cs2kit-utils-tests.exe --list-test-cases
build/windows-msvc-release/vendor/cs2-kit/cs2kit-utils-tests.exe --test-case="SteamId::*"
build/windows-msvc-release/vendor/cs2-kit/cs2kit-utils-tests.exe --source-file="*Targeting*"
build/windows-msvc-release/vendor/cs2-kit/cs2kit-utils-tests.exe --success   # print passing asserts too
```

## Writing a test

Every `tests/*.cpp` is a test TU - nothing but test cases; `cs2_add_tests()` supplies
doctest's `main`. A new file needs no registration, the glob picks it up.

```cpp
#include <CS2Kit/Utils/StringUtils.hpp>
#include <doctest/doctest.h>
#include <string>

using CS2Kit::Utils::ParseDuration;

TEST_CASE("ParseDuration: suffixes")
{
    CHECK_EQ(ParseDuration("30"), 30);
    CHECK_EQ(ParseDuration("5m"), 300);
    CHECK_EQ(ParseDuration("perm"), 0);   // permanent
    CHECK_EQ(ParseDuration("nope"), -1);  // unparseable
}
```

### Assertions

`CHECK*` records a failure and keeps going; `REQUIRE*` aborts the case immediately. Reach
for `REQUIRE` whenever the rest of the case would dereference or index what you just
checked - otherwise a failure turns into a crash with no report:

```cpp
TEST_CASE("FindSettledSnap picks the snap nearest the shot")
{
    auto snap = Detectors::AimSnap::FindSettledSnap(window, cfg);
    REQUIRE(snap.has_value());     // stop here rather than crash on snap->Ago below
    CHECK_EQ(snap->Ago, 1);
}
```

Failures print operand *values*, not just the source text - `CHECK(a == b)` decomposes the
comparison, and the binary forms (`CHECK_EQ`, `CHECK_NE`, `CHECK_LT`, ...) are equivalent:

```text
TEST CASE:  ParseDuration: suffixes

ParseDurationTests.cpp(9): ERROR: CHECK_EQ( ParseDuration("5m"), 300 ) is NOT correct!
  values: CHECK_EQ( 5, 300 )
```

What does lose that detail is wrapping the comparison in a predicate: `CHECK(Near(a, b))`
can only report `values: CHECK( false )`. Compare directly where you can.

`doctest::Approx` is the built-in float comparison, but note its tolerance is *relative* -
`|a - b| < epsilon * (scale + max(|a|, |b|))`, so `Approx(180.0f).epsilon(0.01)` accepts a
1.81 gap, not 0.01. Where an absolute tolerance is what the test means (degrees, score
units), a small local `Near(a, b, eps)` helper is the honest choice; the angle and decaying
-score suites use one deliberately.

For expected throws use `CHECK_THROWS_AS(expr, Type)`, `CHECK_THROWS_WITH`, or
`CHECK_NOTHROW`.

### Sharing setup with SUBCASE

Each `SUBCASE` re-runs the enclosing case body from the top, so setup is written once and
every branch gets a fresh copy - no fixture class, no leakage between branches:

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
    CHECK_EQ(StringUtils::Trim(T{"  hi  "}), std::string("hi"));
}
```

## Adding tests to a plugin

`cs2_add_tests()` (from `cmake/CS2KitTests.cmake`, included by the kit's root CMakeLists) owns
the wiring: it globs `tests/*.cpp`, supplies doctest's `main`, links `doctest::doctest`,
adds the kit's include dir, and registers the cases with CTest. `SOURCES` is the list of
SDK-free TUs to recompile - test binaries never link the plugin module or the kit, so
nothing drags in Metamod.

```cmake
if(BUILD_TESTING)
    cs2_add_tests(myplugin-tests
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

## What is worth testing

Logic that takes plain values and returns plain values: parsers and formatters, the
target-selector grammar, angle math, decaying scores, throttles, migration-version
extraction, detector heuristics, threshold reachability. Anything that needs a live
`Engine()`, an entity, or a database connection is out of scope for this suite - keep that
logic thin and push the decisions into free functions over structs, which is what makes
the rest testable.

## Naming rule: no `[`, `]` or `;`

Discovery registers the CTest entries by running the freshly built binary with
`--list-test-cases` and parsing the output as a CMake list, where `[`...`]` groups and `;`
separates. So an unmatched bracket folds every following case into one entry (configure then
dies with the unrelated-looking `add_test called with incorrect number of arguments`), and a
semicolon splits one case into two bogus entries - silently.

`cs2_add_tests()` scans the test sources and fails configure with the offending file rather
than letting either happen. Spell interval bounds out (`wraps to -180 exclusive through 180
inclusive`, not `wraps into (-180, 180]`). Parentheses, commas, colons, `<`, `>` and `::`
are all fine.
