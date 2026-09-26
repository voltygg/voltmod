# Configuration {#config_guide}

[TOC]

## Declaring settings

Settings are a default-initialized struct that mirrors the JSON file, loaded through
@ref VoltMod::Options. Include `<VoltMod/App/Config.hpp>` in the plugin's own `Config.hpp`; it
carries the configuration types and the JSON layer, which `<VoltMod/Api.hpp>` deliberately does
not.

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

struct Settings
{
    VoltMod::StandardPluginSettings plugin;   // the framework's "plugin" section (locale)
    // one struct + one member per additional section
};

using ConfigManager = VoltMod::Options<Settings>;
```

Each public member name is its JSON key, so nothing has to be registered. A missing key keeps the
member's C++ initializer. JSONC comments and unknown keys are accepted; a missing file, a parse
error or a wrong value type fails the load and names the offending key with its line and column.

Reflection reads member names off the type, so the struct needs external linkage: declare it at
namespace scope, not inside a function or an anonymous namespace.

Document each key with a comment beside it in the shipped `settings.jsonc`; that file is what an
operator reads.

## Loading

```cpp
struct App final : VoltMod::Plugin
{
    using Plugin::Plugin;

    ConfigManager Config = VoltMod::LoadConfig<ConfigManager>(Runtime);
    BhopManager Bhop{Runtime, Config};   // built with the settings already loaded
};
```

`LoadConfig` runs a required `Configuration` load step that reads
`addons/voltmod/plugins/<plugin>/configs/settings.jsonc`, then loads the plugin's `translations` and
applies `plugin.locale` when the settings struct embeds @ref VoltMod::StandardPluginSettings, and
returns the config. When the settings step fails the defaults stand, and the framework refuses the
plugin before `Load` runs, so members below `Config` must not act outside the plugin in their
constructors: a server convar or a published service waits for `Load`.

A config type with a builder is passed in, and `LoadConfigOptions` changes either half:

```cpp
ConfigManager Config = VoltMod::LoadConfig(Runtime, ConfigManager{&BuildSnapshot});
ConfigManager Config = VoltMod::LoadConfig<ConfigManager>(Runtime, {}, {.SettingsFile = "configs/other.jsonc"});
ConfigManager Config = VoltMod::LoadConfig<ConfigManager>(Runtime, {}, {.Translations = false});
```

It calls your config type's `LoadSettings(path)` when it has one, otherwise `Options::Load(path)`.

## Validating and deriving

When settings need checking, clamping or values derived from them, name a second type for what
the plugin publishes and the function that builds it. `Options` runs the builder on a local copy
and publishes the result in one move, so a reload that fails leaves the previous snapshot in
place and no caller ever sees a half-validated value.

```cpp
class ConfigManager
{
public:
    VoltMod::Status LoadSettings(std::string_view path) { return _options.Load(path); }
    VoltMod::Status Reload() { return _options.Reload(); }

    /** Returns the effective settings, which is what LoadConfig reads plugin.locale from. */
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

Without the wrapper, `Get()` returns the snapshot itself, so `options.Get().Values` is the
settings. Wrapping it keeps `plugin.locale` reachable and gives the rest of the plugin named
accessors.

`VoltMod/App/Config/Validation.hpp` has the common helpers. `BuildSnapshot` takes the raw settings
by value, so each one works on a local copy:

```cpp
namespace Validation = VoltMod::Validation;

ConfigManager::Snapshot ConfigManager::BuildSnapshot(Settings raw)
{
    Snapshot result{.Values = std::move(raw)};
    auto& s = result.Values;

    // Clamp and fall back, logging once; "server.tag" is how the field is named in the log line.
    Validation::NormalizeTag(s.server.tag, 32, "default", "server.tag");

    // Drop entries the predicate rejects, logging each. It returns the reason, or nullopt to keep.
    Validation::FilterValid(
        s.punishments.templates,
        [](const PunishmentTemplate& t, std::size_t) -> std::optional<std::string> {
            return IsKnownType(t.type) ? std::nullopt : std::optional(std::format("unknown type '{}'", t.type));
        },
        "punishments.templates");

    // "5m"/"1h"/"perm" -> seconds, 0 being permanent. Invalid entries are logged and skipped, and
    // the struct defaults stand if nothing valid remains.
    result.MenuDurationSecs = Validation::ParseDurations(
        s.punishments.menuDurations, PunishmentSettings{}.menuDurations, "punishments.menuDurations");

    return result;
}
```

## Reloading

`Options` remembers the file it loaded, and `Reload()` parses it again, rebuilds the snapshot and
swaps it in. A failure returns the error and changes nothing, so a command can report the offending
key and keep serving the settings already in memory.

```cpp
if (auto loaded = Config.Reload(); !loaded)
    return Reply{std::format("Settings not reloaded: {}", loaded.error().Detail)};
```

## Framework types in settings

`DatabaseConfig` uses lowercase field names so a JSON section maps straight onto it:

```cpp
struct Settings
{
    VoltMod::DatabaseConfig database;   // "database": { "driver": ..., "host": ..., "port": ... }
};
```

@ref database_guide has the field list and one JSON example per driver.

## Translations

Player-facing text lives in per-language files under `translations/` (`en.json`,
`ru.json`, ...), flat key to string with `{token}` placeholders:

```json
{
    "cmd.banSuccess": "Banned {name}.",
    "target.noMatch": "No player matched."
}
```

```cpp
runtime.Translations.SetLanguage("en");               // server default, from plugin.locale
runtime.Translations.SetPlayerLanguage(slot, "ru");   // the player's pick, for every plugin

auto line = runtime.Translations.Get("cmd.banSuccess", slot, {{"name", targetName}});
```

The player's language lives in the host, so one plugin's language setting reaches every other plugin's
text; the host clears it when the slot changes hands, and whoever saves it applies it again on
connect. A key nothing carries is returned as itself. Command replies (`Caller::Ok`/`Fail`/`Say`), `Flow`
validation errors and `Messages::SendKey` all resolve through this service in the addressed
player's language. The framework reserves a few keys for its own error replies; see
@ref commands_guide.
