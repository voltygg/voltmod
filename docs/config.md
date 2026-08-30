# Configuration {#config_guide}

[TOC]

Represent settings with a default-initialized struct that mirrors the JSON file,
then load it through @ref VoltMod::JsonConfig.

Include `<VoltMod/App/Config.hpp>` in the plugin's `Config.hpp`. It provides the
VoltMod configuration types and the JSON layer without adding either to the main
API umbrella.

## Declaring settings

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

struct Settings
{
    VoltMod::StandardPluginSettings plugin;   // the framework-standard "plugin" section (locale)
    // one struct + member per additional section
};

using ConfigManager = VoltMod::JsonConfig<Settings>;
```

Public members are reflected, so there is nothing to register: the member name
*is* the JSON key. A missing key keeps the member's C++ initializer. Missing
files, parse errors, and wrong value types fail the load. JSONC comments and
unknown keys are accepted, so older files may retain settings a newer build no
longer uses.

A settings struct must have external linkage - reflection reads member names off
the type - so declare it at namespace scope, never inside a function or an
anonymous namespace.

@ref VoltMod::StandardPluginSettings is the framework-owned "plugin" section; embedding it is what lets `LoadStandardConfig` apply `plugin.locale` to `runtime.Translations` automatically (see @ref plugin_guide).

### Editor validation with a JSON Schema

Ship `settings.schema.json` beside the JSONC file and reference it with a
relative `$schema` as the first key. The runtime ignores `$schema` with other
unknown keys. Give the schema `additionalProperties: false` for editor-side typo
detection and keep it synchronized with the settings struct.

```cpp
bool MyPlugin::OnLoad(VoltMod::Runtime& runtime)
{
    // Config + translations as LoadReport stages; uses LoadSettings when your
    // ConfigManager defines one, plain Load otherwise.
    return VoltMod::LoadStandardConfig(runtime, Config, {.Addon = "my-plugin"});
}
```

## Post-load validation

When raw settings need parsing or clamping (duration strings, tag sanitizing,
dropping invalid list entries), **compose** `Json::ReadFile` rather than
subclassing `JsonConfig`, and publish the validated result in one assignment.
Name the entry point `LoadSettings` and `LoadStandardConfig` picks it up instead
of `Load`:

```cpp
class ConfigManager
{
public:
    VoltMod::Status LoadSettings(std::string_view path)
    {
        auto raw = VoltMod::Json::ReadFile<Settings>(path);
        if (!raw)
            return std::unexpected(raw.error());

        // Validate a local copy, then publish it whole.
        _snapshot = BuildSnapshot(std::move(*raw));
        return {};
    }

    const Settings& Get() const { return _snapshot.Values; }
    const std::vector<int>& GetMenuDurations() const { return _snapshot.MenuDurationSecs; }

private:
    struct ConfigSnapshot
    {
        Settings Values;
        std::vector<int> MenuDurationSecs;
    };

    static ConfigSnapshot BuildSnapshot(Settings raw);

    ConfigSnapshot _snapshot;
};
```

Resolving into a value that has not been published yet is the point: a failed
reload leaves the previous configuration whole, and no caller can observe a
half-validated one. `Get()` should keep returning the effective settings because
`LoadStandardConfig` reads `Get().plugin.locale` when that section exists.

`VoltMod/Core/Validation.hpp` (`VoltMod::Validation`) has the common resolution helpers:

`BuildSnapshot` takes the raw settings by value and returns the snapshot, so
every helper below mutates a local:

```cpp
namespace Validation = VoltMod::Validation;

ConfigManager::ConfigSnapshot ConfigManager::BuildSnapshot(Settings raw)
{
    ConfigSnapshot result{.Values = std::move(raw)};
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

## Framework types in your settings

`PostgresConfig` uses lowercase field names precisely so a JSON section maps onto
it. Reflection needs no mapper, so embedding it is all there is to do:

```cpp
struct Settings
{
    VoltMod::PostgresConfig database;   // "database": { "host": ..., "port": ... }
    // ...
};
```

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
