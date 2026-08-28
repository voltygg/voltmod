# Configuration {#config_guide}

[TOC]

Settings are represented by a struct that mirrors the JSON file. Declare each
section with defaults, map it with nlohmann's non-intrusive macro, and hold the
root in a @ref VoltMod::JsonConfig.

`<VoltMod/Api.hpp>` never reaches nlohmann, so a plugin's own `Config.hpp` includes
`<VoltMod/App/Config.hpp>` as well - it gathers `JsonConfig`, `StandardPluginSettings`, and
`VoltMod::Json`, and pulls in nlohmann for the `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`
macro below.

## Declaring settings

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

struct Settings
{
    VoltMod::StandardPluginSettings plugin;   // the framework-standard "plugin" section (locale)
    // one struct + member per additional section
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, plugin)

using ConfigManager = VoltMod::JsonConfig<Settings>;
```

Member names must match the JSON keys. The `_WITH_DEFAULT` macro means a missing key keeps the member's default. Only a missing file, a parse error, or a wrong-typed value fails the load. JSONC comments are tolerated, and unknown keys are ignored (which is also why retired keys need no config migration).

@ref VoltMod::StandardPluginSettings is the framework-owned "plugin" section; embedding it is what lets `LoadStandardConfig` apply `plugin.locale` to `runtime.Translations` automatically (see @ref plugin_guide).

### Editor validation with a JSON Schema

Ship a `settings.schema.json` next to the jsonc and reference it with a relative `$schema` line as the file's first key. Editors then autocomplete keys and squiggle typos (`additionalProperties: false` makes the schema stricter than the runtime, which is the point; the parser itself ignores unknown keys). The plugin scaffold emits a starter schema; keep it in sync when the Settings struct grows. The framework's own `gamedata/gamedata.jsonc` follows the same convention, with `gamedata.schema.json` beside it.

```cpp
bool MyPlugin::OnLoad(VoltMod::Runtime& runtime)
{
    // Config + translations as LoadReport stages; uses LoadSettings when your
    // ConfigManager defines one, plain Load otherwise.
    return VoltMod::LoadStandardConfig(runtime, Config, {.Addon = "my-plugin"});
}
```

## Post-load validation

When raw settings need parsing or clamping (duration strings, tag sanitizing, dropping invalid list entries), subclass `JsonConfig` and resolve once after `Load`. Name the entry point `LoadSettings` and `LoadStandardConfig` picks it up instead of `Load`:

```cpp
class ConfigManager : public VoltMod::JsonConfig<Settings>
{
public:
    bool LoadSettings(const std::string& path)
    {
        if (!Load(path))
            return false;
        Resolve();
        return true;
    }

    const std::vector<int>& GetMenuDurations() const { return _menuDurationSecs; }

private:
    void Resolve();
    std::vector<int> _menuDurationSecs;
};
```

`VoltMod/Core/Validation.hpp` (`VoltMod::Validation`) has the common resolution helpers:

```cpp
namespace Validation = VoltMod::Validation;

void ConfigManager::Resolve()
{
    auto& s = Mutable();

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
    _menuDurationSecs = Validation::ParseDurations(s.punishments.menuDurations,
                                                   PunishmentSettings{}.menuDurations, "punishments.menuDurations");
}
```

## Framework types in your settings

`PostgresConfig` uses lowercase field names precisely so a JSON section maps onto it. The framework header stays nlohmann-free, so define the mapper in your plugin, inside the `VoltMod` namespace where ADL finds it:

```cpp
namespace VoltMod
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PostgresConfig, host, port, database, username, password, sslMode)
}

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
