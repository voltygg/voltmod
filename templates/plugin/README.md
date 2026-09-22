# $title

What the plugin does for players and server operators, in a sentence or two.

## Install

Build and install it into `CS2_SERVER_PATH`:

```bash
uv run poe build --install $name --start
```

Run `volt list` in the server console. `$name` should be in the loaded plugin list.

## Commands

| Command | Who | What it does |
| --- | --- | --- |
| `!ping` | Everyone | Check that the plugin is alive |

## Configuration

Settings are in `addons/voltmod/plugins/$name/configs/settings.jsonc` on the server. The file is
seeded on the first install and never overwritten.

| Setting | Default | Purpose |
| --- | --- | --- |
| `plugin.locale` | `en` | Server language: a file in `configs/translations` |

Player-facing text is in `configs/translations/`.
