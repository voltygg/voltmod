"""Installing plugins into a local CS2 server and running it; fleet deployment is the consumer's."""

import shutil
import subprocess
from pathlib import Path

from voltmod.conan import editable_framework
from voltmod.cs2_install import (
    CSGO_DIR,
    HOST_COMPONENT,
    SERVER_EXECUTABLES,
    find_server,
    plugin_dir,
    server_executable,
)
from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, run_tool
from voltmod.project import Project, Settings


def install_plugins(project: Project, server_path: str, plugin: str, preset: str) -> None:
    """Install the host and `plugin`, or every plugin when it is empty, into that server."""
    server = find_server(server_path)
    csgo = server / CSGO_DIR
    names = project.plugin_names(plugin)

    print("=== VoltMod Install ===\n")
    print(f"Server path:   {server}")
    print(f"Build preset:  {preset}\n")

    _install_host(project, csgo, preset)
    for name in names:
        _install_plugin(project, name, csgo, preset, named=bool(plugin))

    print(f"\n=== Install complete ===\nInstalled: {' '.join(names)}")
    print("Verify on the server console with: volt list")


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


def _install_host(project: Project, csgo: Path, preset: str) -> None:
    """Install the host: the only Metamod plugin, which loads its managed plugins."""
    print("--- voltmod host ---")
    # An editable framework checkout builds the host in its own tree.
    checkout = editable_framework(project.root)
    searched = [project.build_dir(preset)] + ([checkout / "build" / preset] if checkout else [])
    for build_dir in searched:
        if staging := _stage_component(build_dir, HOST_COMPONENT):
            _merge_addons(staging, csgo, HOST_COMPONENT)
            _print_staged(staging)
            return

    looked_in = "\n  ".join(map(str, searched))
    raise VoltmodError(
        f"no voltmod host was staged; looked in:\n  {looked_in}\n"
        f"Build the framework first: voltmod build {preset}"
    )


def _install_plugin(project: Project, name: str, csgo: Path, preset: str, *, named: bool) -> None:
    """Stage one plugin with `cmake --install`, then merge it into the server tree.

    A plugin asked for by name must install; a bare install skips what is not built.
    """
    print(f"--- {name} ---")
    staging = _stage_component(project.build_dir(preset), name)
    if staging is None:
        if named:
            raise VoltmodError(f"{name} is not built; run `voltmod build {preset}` first")
        print(f"  (skipped - not built for {preset})")
        return

    _merge_addons(staging, csgo, name)
    _print_staged(staging)
    _seed_settings(project, name, csgo)


def _seed_settings(project: Project, name: str, csgo: Path) -> None:
    """Copy the shipped settings once, so an operator's edits survive every later install."""
    project_plugin_dir = project.plugin_dir(name) or project.root / "plugins" / name
    source = project_plugin_dir / "configs/settings.jsonc"
    if not source.is_file():
        return
    target = csgo / plugin_dir(name) / "configs/settings.jsonc"
    if target.is_file():
        print("  -> configs/settings.jsonc (skipped - already exists)")
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    print("  -> configs/settings.jsonc (seeded)")


def _print_staged(staging: Path) -> None:
    """Name what was merged, using the staged tree's own top-level paths."""
    top = sorted({path.relative_to(staging).parts[:2] for path in staging.rglob("*")})
    for parts in top:
        print(f"  -> {'/'.join(parts)}")


def _stage_component(build_dir: Path, component: str) -> Path | None:
    """Stage one install component under the build tree; None when it installs no addons."""
    if not build_dir.is_dir():
        return None
    staging = build_dir / "_install-staging" / component
    shutil.rmtree(staging, ignore_errors=True)
    try:
        run_tool(
            "cmake", "--install", str(build_dir), "--component", component,
            "--prefix", str(staging),
        )
    except subprocess.CalledProcessError:
        return None
    return staging if (staging / "addons").is_dir() else None


def _merge_addons(staging: Path, csgo: Path, what: str) -> None:
    try:
        shutil.copytree(staging / "addons", csgo / "addons", dirs_exist_ok=True)
    except (shutil.Error, PermissionError) as error:
        raise VoltmodError(
            f"could not replace the installed files for {what}: {error}\n"
            "A running CS2 server holds the binary open; stop it and try again."
        ) from None
