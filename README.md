# VoltMod

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://voltygg.github.io/voltmod/)

VoltMod is a C++23 framework for Counter-Strike 2 server plugins on Metamod:Source. One
process-wide host, `voltmod.dll` / `voltmod.so`, is the server's only Metamod plugin and loads
your plugins from `addons/voltmod/plugins/<name>/`. Each plugin gets one `Runtime` per load cycle with commands,
players, menus, messages, engine access, HTTP and an optional database.

> Public APIs may change between versions.

## The smallest plugin

`plugins/my-plugin/plugin.json`:

```json
{
  "name": "my-plugin",
  "version": "1.0.0"
}
```

`plugins/my-plugin/src/App.cpp`:

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginEntry.hpp>

namespace MyPlugin
{

struct App final : VoltMod::Plugin
{
    explicit App(VoltMod::Runtime& runtime) : Plugin(runtime) {}

    bool Load() override
    {
        // "cmd.pong" is a translation key; an untranslated key is replied verbatim.
        Runtime.Commands.Add("ping").Describe("Check that the plugin is alive.").Run(
            [](VoltMod::Caller c) -> VoltMod::Result<VoltMod::Reply> { return c.Ok("cmd.pong"); });
        return true;
    }
};

}  // namespace MyPlugin

VOLTMOD_PLUGIN(MyPlugin::App);
```

`plugins/my-plugin/CMakeLists.txt`:

```cmake
voltmod_add_plugin(my-plugin)
```

`voltmod init` writes all three, plus settings and translations.

## Install and run

```sh
uvx --from git+https://github.com/voltygg/voltmod.git voltmod init --plugin my-plugin
uv sync
uv run poe bootstrap
uv run poe build --install my-plugin --start
```

On the server console, `volt list` shows the plugin; in chat, `!ping` answers.
[Getting started](docs/getting-started.md) has the prerequisites and each step on its own.

## Documentation

Published at [voltygg.github.io/voltmod](https://voltygg.github.io/voltmod/).

- [Getting started](docs/getting-started.md)
- [Writing a plugin](docs/plugin.md)
- [Running the host](docs/host.md)
- [Configuration](docs/config.md)
- [Commands](docs/commands.md)
- [Architecture](docs/architecture.md)
- [Consuming VoltMod with Conan](docs/consuming-via-conan.md)
- [Changelog](CHANGELOG.md)

## Contributing

Open an issue before starting a large change; the public API is still moving.

## License

[MIT](LICENSE).
