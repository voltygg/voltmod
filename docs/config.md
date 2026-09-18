# Configuration {#config_guide}

[TOC]

Represent settings with a default-initialized struct that mirrors the JSON file,
then load it through @ref VoltMod::Options. Include
`<VoltMod/App/Config.hpp>` in the plugin's `Config.hpp`; it provides the
configuration types and JSON layer without adding them to the main API umbrella.

## Declaring settings

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

struct Settings
{
    VoltMod::StandardPluginSettings plugin;   // the framework-standard "plugin" section (locale)
    // one struct + member per additional section
};

using ConfigManager = VoltMod::Options<Settings>;
```

Reflection uses each public member name as its JSON key, so no registration is
required. Missing keys keep their C++ initializers. Missing files, parse errors,
and wrong value types fail the load; JSONC comments and unknown keys are accepted.

Reflection reads member names from the type, so the settings struct must have
external linkage. Declare it at namespace scope, not inside a function or an
anonymous namespace. Embedding @ref VoltMod::StandardPluginSettings provides the
framework-owned `plugin` section and lets `LoadStandardConfig` apply
`plugin.locale` to `runtime.Translations` (see @ref plugin_guide).

### Editor validation with a JSON Schema

Ship `settings.schema.json` beside the JSONC file and reference it with a
relative `$schema` as the first key. The runtime ignores `$schema` with other
unknown keys. Give the schema `additionalProperties: false` for editor-side typo
detection and keep it synchronized with the settings struct.

```cpp
bool MyPlugin::OnLoad(VoltMod::Runtime& runtime)
{
    return VoltMod::LoadStandardConfig(runtime, Config, {.Addon = "my-plugin"});
}
```

## Post-load validation

When settings need checking, clamping or values derived from them, name a second
type for what the plugin publishes and the function that builds it. `Options`
runs the builder on a local copy and publishes the result in one move, so a
reload that fails to parse leaves the previous snapshot intact and callers never
observe a partially validated value:

```cpp
struct Snapshot
{
    Settings Values;
    std::vector<int> MenuDurationSecs;
};

static Snapshot BuildSnapshot(Settings raw);

VoltMod::Options<Settings, Snapshot> options{&BuildSnapshot};
```

`Get()` now returns the snapshot, so `options.Get().Values` is the settings and
`options.Get().MenuDurationSecs` the derived list. Wrap it in a small class of
your own when you want named accessors, and name the entry point `LoadSettings`;
`LoadStandardConfig` uses it instead of `Load`:

```cpp
class ConfigManager
{
public:
    VoltMod::Status LoadSettings(std::string_view path) { return _options.Load(path); }

    const Settings& Get() const { return _options.Get().Values; }
    const std::vector<int>& GetMenuDurations() const { return _options.Get().MenuDurationSecs; }

private:
    struct Snapshot
    {
        Settings Values;
        std::vector<int> MenuDurationSecs;
    };

    static Snapshot BuildSnapshot(Settings raw);

    VoltMod::Options<Settings, Snapshot> _options{&BuildSnapshot};
};
```

That wrapper is also what keeps `plugin.locale` working: `LoadStandardConfig`
reads `Get().plugin.locale` when that section exists, so `Get()` has to return
the effective settings rather than the snapshot around them.

Use the common helpers in `VoltMod/App/Config/Validation.hpp`. `BuildSnapshot` takes the
raw settings by value, so each helper below operates on a local copy:

```cpp
namespace Validation = VoltMod::Validation;

ConfigManager::Snapshot ConfigManager::BuildSnapshot(Settings raw)
{
    Snapshot result{.Values = std::move(raw)};
    auto& s = result.Values;

    // Clamp + fall back with a logged warning; "server.tag" names the field in the log line.
    Validation::NormalizeTag(s.server.tag, 32, "default", "server.tag");

    // Drop entries a predicate rejects, logging each removal. The predicate returns the
    // rejection reason (or nullopt to keep the entry), not a plain bool.
    Validation::FilterValid(
        s.punishments.templates,
        [](const PunishmentTemplate& t, std::size_t) -> std::optional<std::string> {
            return IsKnownType(t.type) ? std::nullopt : std::optional(std::format("unknown type '{}'", t.type));
        },
        "punishments.templates");

    // "5m"/"1h"/"perm" strings -> seconds (0 = permanent); invalid entries logged and skipped,
    // falling back to the struct defaults if nothing valid remains.
    result.MenuDurationSecs = Validation::ParseDurations(
        s.punishments.menuDurations, PunishmentSettings{}.menuDurations, "punishments.menuDurations");

    return result;
}
```

## Reloading

`Load` is the reload: it parses the file again, rebuilds the snapshot and swaps
it in. A failed parse returns the error and changes nothing, so a plugin command
can report the offending key and go on serving the settings already in memory:

```cpp
if (auto loaded = Config.Load(VoltMod::AddonFile("my-plugin", "configs/settings.jsonc")); !loaded)
    return Reply{std::format("Settings not reloaded: {}", loaded.error().Detail)};
```

## Framework types in your settings

`DatabaseConfig` uses lowercase field names precisely so a JSON section maps onto
it. Reflection needs no mapper, so embedding it is all there is to do:

```cpp
struct Settings
{
    VoltMod::DatabaseConfig database;   // "database": { "driver": ..., "host": ..., "port": ... }
    // ...
};
```

See @ref database_guide for the full field list and one JSON example per driver.

## Translations

Human-facing text lives in per-language JSON files (`translations/en.json`, `translations/ru.json`, ...), flat key → string with `{token}` placeholders:

```json
{
    "cmd.banSuccess": "Banned {name}.",
    "target.noMatch": "No player matched."
}
```

```cpp
runtime.Translations.Load("addons/my-plugin/configs/translations");
runtime.Translations.SetLanguage("en");                       // server default
runtime.Translations.SetPlayerLanguage(slot, "ru");           // per-player override

auto line = runtime.Translations.Get("cmd.banSuccess", slot, {{"name", targetName}});
```

Command replies (`Caller::Ok`/`Fail`/`Say`), `Flow` validation errors, and
`Messages::ReplyKey` all resolve through this service in the addressed
player's language. The framework reserves a small set of keys for its own error
replies; see @ref commands_guide.
