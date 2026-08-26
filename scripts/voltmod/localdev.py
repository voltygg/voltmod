"""Install plugins into a local CS2 server and launch it.

The single-machine development loop: build, drop the result into a CS2
dedicated server tree, and run it. Fleet deployment belongs to the consuming
project; this module only ever touches one server directory.
"""

import os
import shutil
import subprocess
from pathlib import Path

from . import buildtools
from .buildtools import WINDOWS, die

#: Server-relative paths to the dedicated server executable, most likely first.
_EXECUTABLES = ("game/bin/win64/cs2.exe", "game/bin/linuxsteamrt64/cs2")


def load_env(root: Path) -> None:
    """Apply `KEY=VALUE` defaults from `<root>/.env` without overriding the real environment."""
    env_file = root / ".env"
    if not env_file.is_file():
        return
    for line in env_file.read_text(encoding="utf-8").splitlines():
        entry = line.strip()
        if not entry or entry.startswith("#"):
            continue
        key, sep, value = entry.partition("=")
        if sep:
            os.environ.setdefault(key.strip(), value.strip().strip("\"'"))


def default_preset() -> str:
    """The preset to use when the caller does not name one."""
    return os.environ.get("CS2_BUILD_PRESET") or buildtools.default_preset()


def plugin_names(root: Path, requested: str) -> list[str]:
    """Resolve the requested plugin, or every plugin under `plugins/`."""
    if requested:
        if not (root / "plugins" / requested).is_dir():
            die(f"plugin 'plugins/{requested}' not found")
        return [requested]
    plugins = root / "plugins"
    if not plugins.is_dir():
        die(f"no plugins directory at {plugins}")
    names = sorted(
        path.name for path in plugins.iterdir() if path.is_dir() and (path / "src").is_dir()
    )
    if not names:
        die("no plugins found under plugins/")
    return names


def server_root(server_path: str) -> Path:
    """Validate a CS2 server installation root."""
    if not server_path:
        die("no CS2 server path; set CS2_SERVER_PATH in .env or pass --server-path")
    root = Path(server_path).expanduser()
    if not (root / "game/csgo").is_dir():
        die(
            f"CS2 server not found at {root / 'game/csgo'}\n"
            "Set CS2_SERVER_PATH in .env or pass --server-path"
        )
    return root


def install_plugin(root: Path, name: str, csgo: Path, preset: str, *, named: bool) -> None:
    """Stage one plugin via `cmake --install` and merge it into the server tree."""
    print(f"--- {name} ---")
    build_dir = root / "build" / preset
    if not build_dir.is_dir():
        if named:
            die(f"no build at {build_dir}\nBuild first: voltmod build {preset}")
        print(f"  (skipped - no build at {build_dir})")
        return

    staging = build_dir / "_install-staging" / name
    shutil.rmtree(staging, ignore_errors=True)
    try:
        buildtools.run_tool(
            "cmake",
            "--install",
            str(build_dir),
            "--component",
            name,
            "--prefix",
            str(staging),
        )
    except subprocess.CalledProcessError:
        if named:
            die(f"cmake --install failed for {name} (is it built?)")
        print(f"  (skipped - cmake --install produced nothing for {name})")
        return

    try:
        shutil.copytree(staging / "addons", csgo / "addons", dirs_exist_ok=True)
    except (shutil.Error, PermissionError) as exc:
        die(
            f"could not replace the installed files for {name}: {exc}\n"
            "A running CS2 server holds the plugin binary open; stop it and try again."
        )
    print("  -> addons/ (binary, vdf, configs, voltmod gamedata)")

    # Preserve operator-edited settings after the first install.
    settings_src = root / "plugins" / name / "configs" / "settings.jsonc"
    settings_dst = csgo / "addons" / name / "configs" / "settings.jsonc"
    if settings_src.is_file():
        if settings_dst.is_file():
            print("  -> configs/settings.jsonc (skipped - already exists)")
        else:
            settings_dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(settings_src, settings_dst)
            print("  -> configs/settings.jsonc (seeded)")


def install(root: Path, server_path: str, plugin_name: str = "", preset: str = "") -> None:
    """Install the selected plugins into a local CS2 server tree."""
    server = server_root(server_path)
    csgo = server / "game/csgo"
    preset = preset or default_preset()

    plugins = plugin_names(root, plugin_name)
    print("=== Metamod:Source Plugin Install ===\n")
    print(f"Server path:   {server}")
    print(f"Build preset:  {preset}\n")

    (csgo / "addons" / "metamod").mkdir(parents=True, exist_ok=True)
    for name in plugins:
        install_plugin(root, name, csgo, preset, named=bool(plugin_name))

    print(f"\n=== Install complete ===\nInstalled: {' '.join(plugins)}")
    print("Verify on the server console with: meta list")


def update_server(steamcmd_path: str, server: Path) -> None:
    """Refresh the server files when SteamCMD is available."""
    steamcmd = Path(steamcmd_path).expanduser() if steamcmd_path else None
    if not steamcmd or not steamcmd.is_file():
        print(f"WARNING: SteamCMD not found at {steamcmd}; skipping update.")
        return

    result = subprocess.run(
        [
            str(steamcmd),
            "+force_install_dir",
            str(server),
            "+login",
            "anonymous",
            "+app_update",
            "730",
            "validate",
            "+quit",
        ]
    )
    if result.returncode:
        print(f"WARNING: SteamCMD update failed ({result.returncode}); using existing files.")


def serve(
    server_path: str,
    *,
    steamcmd_path: str = "",
    map_name: str = "de_dust2",
    gslt_token: str = "",
    max_players: int = 16,
    port: int = 27015,
    rcon_password: str = "",
    check_update: bool = False,
) -> None:
    """Optionally update, then run the local CS2 dedicated server in the foreground."""
    server = server_root(server_path)
    if check_update:
        update_server(steamcmd_path, server)

    executable = next((server / rel for rel in _EXECUTABLES if (server / rel).is_file()), None)
    if executable is None:
        expected = _EXECUTABLES[0] if WINDOWS else _EXECUTABLES[1]
        die(f"CS2 executable not found: {server / expected}")

    command = [
        str(executable),
        "-dedicated",
        "-console",
        "-usercon",
        "+map",
        map_name,
        "-maxplayers",
        str(max_players),
        "-port",
        str(port),
        "+game_mode",
        "0",
    ]
    if gslt_token:
        command += ["+sv_setsteamaccount", gslt_token]
    if rcon_password:
        command += ["+rcon_password", rcon_password]

    mode = "public" if gslt_token else "LAN"
    print(f"=== Starting CS2: {map_name}, {max_players} players, port {port}, {mode} ===")
    subprocess.run(command, cwd=executable.parent)
