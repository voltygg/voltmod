# Configuration {#config_guide}

[TOC]

Represent settings with a default-initialized struct that mirrors the JSON file,
then load it through @ref VoltMod::JsonConfig. Include
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

using ConfigManager = VoltMod::JsonConfig<Settings>;
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

When settings need parsing or clamping, compose `Json::ReadFile` instead of
subclassing `JsonConfig`. Publish the validated result in one assignment and
name the entry point `LoadSettings`; `LoadStandardConfig` uses it instead of
`Load`:

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

Build the snapshot before publishing it. A failed reload then leaves the previous
configuration intact, and callers never observe a partially validated value.
`Get()` must return the effective settings because `LoadStandardConfig` reads
`Get().plugin.locale` when that section exists.

Use the common helpers in `VoltMod/Core/Validation.hpp`. `BuildSnapshot` takes the
raw settings by value, so each helper below operates on a local copy:

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
