# Running the host {#host_guide}

[TOC]

The host is the server's only Metamod plugin. It installs the engine hooks once, loads every
plugin it finds under `addons/voltmod/plugins/*/plugin.json`, and offers each engine event to them in load order.
`meta list` shows the host; `volt` commands show the plugins.

## volt

Run these on the server console or over RCON.

| Command | Does |
| --- | --- |
| `volt list` | Prints `N plugin(s), in load order:` then one `  <name> v<version> - <description>` line each, or `No plugins are loaded.` |
| `volt status` | Prints `<name>: <status JSON>` for every loaded plugin |
| `volt status <name>` | Prints that one line (see @ref plugin_guide for the sections) |
| `volt load <name>` | Loads an installed plugin that is not loaded |
| `volt unload <name>` | Unloads a plugin, unless a loaded plugin requires it |
| `volt reload <name>` | Unloads the plugin and everything that requires it, then loads them all again |
| `volt log <name> <level>` | Sets that plugin's minimum log level - `info`, `warn` or `error` - and prints `<name> now logs <level> and above.` |

`load`, `unload` and `reload` do not run inside the command. They answer
`Queued <action> of '<name>' for the next frame.` and run at the start of the next frame, so a
plugin is never torn down from inside one of its own callbacks. A reload reads the manifests
again, so a rebuilt plugin may declare different dependencies than the one going down.

`volt` itself is reserved before any plugin loads, so a plugin trying to register a command by
that name is refused rather than racing the host for it. The same applies to any command two
plugins both want:

```text
Command 'ban' is already registered by admin-system, so bans cannot have it.
```

## Dependencies

`dependencies` names the plugins this one cannot run without; `optionalDependencies` names the
ones it is better with and runs fine without. Neither decides load order. Plugins load
alphabetically, and one plugin reaches another by asking `runtime.Exchange` for its interface at
the moment it needs it, which answers null when that plugin is not loaded. Engine events, and
console commands offered until one plugin consumes them, follow that same alphabetical order.

| | `dependencies` | `optionalDependencies` |
| --- | --- | --- |
| Not installed | this plugin is refused | ignored |
| Installed but refused | this plugin is refused | ignored |
| `volt unload` on it | refused while this plugin is loaded | allowed |
| `volt reload` on it | this plugin goes down and comes back with it | untouched |

So a plugin that declares `"dependencies": ["admin-system"]` does not load without it and comes
back with it on a reload, in no particular order within that group. Reloading a plugin nothing
requires touches only that plugin.

`volt unload` on something still required names what is in the way:

```text
Refusing to unload 'admin-system': anticheat, reports still requires it.
```

`volt load` on a plugin whose required dependency is not loaded refuses the same way:

```text
Refusing to load 'anticheat': it requires 'admin-system', which is not loaded.
```

## Cross-plugin services

Every plugin is its own library with its own runtime and its own allocator. The host keeps one
service table for the process, and @ref VoltMod::ServiceExchange is the typed view of it. Share
behavior through an interface rather than handing out a manager or a framework object.

```cpp
struct IBanService
{
    static constexpr const char* InterfaceName = "admin.IBanService/1";
    virtual bool IsBanned(uint64_t steamId) const = 0;

protected:
    ~IBanService() = default;
};

runtime.Exchange.Publish<IBanService>(&_bans);          // provider, in Load

if (auto* bans = runtime.Exchange.Get<IBanService>())   // consumer, where it is used
    Check(*bans);
```

Name `T` explicitly in `Publish<T>`, so the stored pointer is the interface subobject the consumer
casts back to. Put a version in `InterfaceName` and bump it whenever the vtable or a parameter's
meaning changes: a stale consumer then gets `nullptr` instead of a mismatched vtable.

Several providers of one interface pass a key: `Publish<IMenuSection>(this, "admin")`, then
`Get<IMenuSection>("admin")` and `Unpublish<IMenuSection>("admin")`.

Ask for a service where you use it and do not keep the pointer. The publisher can unload between
callbacks, never inside one, so a pointer fetched at the point of use cannot dangle before you
are done with it. Never transfer ownership across the boundary, never pass an object one module's
allocator owns, and let no exception escape an interface call. Use a @ref VoltMod::ServerCommand
instead when console, RCON or cfg files need the operation too.

Withdraw with `Unpublish<T>()` or let the unload do it; the host reports anything left behind.

## Troubleshooting

**Nothing loads, no `volt` command.** The host is not there. The server needs
`addons/metamod/voltmod.vdf` and `addons/voltmod/bin/<platform>/voltmod.<dll|so>`; check
`meta list` first. @ref plugin_guide has the full layout.

**Plugins load twice, or an old plugin loads on its own.** Before VoltMod 1.5 every plugin had its
own Metamod plugin file. Delete any leftover `addons/metamod/<plugin>.vdf` by hand;
`voltmod.vdf` is the only one that belongs there.

**`Refusing '<dir>': plugin.json names it '<name>', and a plugin lives in the directory it is
named after.`** Rename the directory or the `name` key so they match. The CMake target must match
too, and `voltmod_add_plugin` fails at configure time when it does not.

**A refusal naming an unknown key.** The manifest is read strictly: a key that is not in the table
in @ref plugin_guide is an error, and so is a wrong value type. The reason names the offending key
with its line and column.

**`Refusing '<name>': it was built against host ABI version N and this host speaks version M;
rebuild the plugin against this VoltMod.`** The plugin and the host come from different framework
versions. Rebuild the plugin and install both from the same build.

**`Refusing '<name>': this plugin was built against another schema layout (plugin ..., host ...);
rebuild it against this VoltMod.`** Schema offsets are baked into each plugin at build time and
the host checks its own copy once per process. Same fix: rebuild against this host. The related
`the host found schema drift; its log names every field` means the game moved under a host that is
otherwise fine - regenerate with `voltmod schemagen` and rebuild.

**`Refusing '<name>': requires '<dep>', which is not installed`** or **`... which the host
refused`.** Install the dependency, or fix why it was refused: every refusal upstream refuses
everything below it.

**`Refusing '<name>': installed more than once; each plugin directory needs its own plugin name.`**
Two directories under `addons/voltmod/plugins/` carry manifests with the same `name`.

**A plugin loads but a feature is missing.** The load summary logs `<feature> is unavailable:
<reason>` for each engine feature that could not bind, and `volt status <name>` repeats it in the
`load` section. That usually means gamedata went stale after a game update; see
@ref sdk_gamedata_guide.
