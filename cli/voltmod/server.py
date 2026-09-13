"""Installing plugins into a local CS2 server and running it; fleet deployment is the consumer's."""

import shutil
import subprocess
from pathlib import Path

from voltmod.cs2_install import SERVER_EXECUTABLES, find_server, server_executable
from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, run_tool
from voltmod.project import Project, Settings


def install_plugins(project: Project, server_path: str, plugin: str, preset: str) -> None:
    """Install @p plugin, or every plugin when it is empty, into the server at @p server_path."""
    server = find_server(server_path)
    csgo = server / "game/csgo"
    names = project.plugin_names(plugin)

    print("=== Metamod:Source Plugin Install ===\n")
    print(f"Server path:   {server}")
    print(f"Build preset:  {preset}\n")

    (csgo / "addons/metamod").mkdir(parents=True, exist_ok=True)
    for name in names:
        _install_plugin(project, name, csgo, preset, named=bool(plugin))

    print(f"\n=== Install complete ===\nInstalled: {' '.join(names)}")
    print("Verify on the server console with: meta list")


def update_server(steamcmd_path: str, server: Path) -> None:
    """Refresh the server files when SteamCMD is available."""
    steamcmd = Path(steamcmd_path).expanduser() if steamcmd_path else None
    if not steamcmd or not steamcmd.is_file():
        print(f"WARNING: SteamCMD not found at {steamcmd}; skipping update.")
        return

    update = [
        str(steamcmd), "+force_install_dir", str(server), "+login", "anonymous",
        "+app_update", "730", "validate", "+quit",
    ]
    result = subprocess.run(update)
    if result.returncode:
        print(f"WARNING: SteamCMD update failed ({result.returncode}); using existing files.")


def run_server(settings: Settings, *, check_update: bool = False) -> None:
    """Optionally update, then run the dedicated server in the foreground."""
    server = find_server(settings.server_path)
    if check_update:
        update_server(settings.steamcmd_path, server)

    executable = server_executable(server)
    if executable is None:
        expected = SERVER_EXECUTABLES[0] if WINDOWS else SERVER_EXECUTABLES[1]
        raise VoltmodError(f"CS2 executable not found: {server / expected}")

    command = [
        str(executable), "-dedicated", "-console", "-usercon",
        "+map", settings.map_name,
        "-maxplayers", str(settings.max_players),
        "-port", str(settings.port),
        "+game_mode", "0",
    ]
    if settings.gslt_token:
        command += ["+sv_setsteamaccount", settings.gslt_token]
    if settings.rcon_password:
        command += ["+rcon_password", settings.rcon_password]

    mode = "public" if settings.gslt_token else "LAN"
    print(
        f"=== Starting CS2: {settings.map_name}, {settings.max_players} players, "
        f"port {settings.port}, {mode} ==="
    )
    subprocess.run(command, cwd=executable.parent)


def _install_plugin(project: Project, name: str, csgo: Path, preset: str, *, named: bool) -> None:
    """Stage one plugin with `cmake --install`, then merge it into the server tree."""
    print(f"--- {name} ---")
    build_dir = project.build_dir(preset)
    if not build_dir.is_dir():
        if named:
            raise VoltmodError(f"no build at {build_dir}\nBuild first: voltmod build {preset}")
        print(f"  (skipped - no build at {build_dir})")
        return

    staging = build_dir / "_install-staging" / name
    shutil.rmtree(staging, ignore_errors=True)
    try:
        run_tool(
            "cmake", "--install", str(build_dir), "--component", name, "--prefix", str(staging)
        )
    except subprocess.CalledProcessError:
        if named:
            raise VoltmodError(f"cmake --install failed for {name} (is it built?)") from None
        print(f"  (skipped - cmake --install produced nothing for {name})")
        return

    try:
        shutil.copytree(staging / "addons", csgo / "addons", dirs_exist_ok=True)
    except (shutil.Error, PermissionError) as error:
        raise VoltmodError(
            f"could not replace the installed files for {name}: {error}\n"
            "A running CS2 server holds the plugin binary open; stop it and try again."
        ) from None
    print("  -> addons/ (binary, vdf, configs, panorama sources, voltmod gamedata)")

    # Operator-edited settings survive every install after the first.
    plugin_dir = project.plugin_dir(name) or project.root / "plugins" / name
    source = plugin_dir / "configs/settings.jsonc"
    target = csgo / "addons" / name / "configs/settings.jsonc"
    if not source.is_file():
        return
    if target.is_file():
        print("  -> configs/settings.jsonc (skipped - already exists)")
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    print("  -> configs/settings.jsonc (seeded)")
