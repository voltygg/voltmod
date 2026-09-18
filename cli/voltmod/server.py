"""Installing plugins into a local CS2 server and running it; fleet deployment is the consumer's."""

import shutil
import subprocess
from pathlib import Path

from voltmod.conan import find_editable_framework
from voltmod.cs2_install import (
    CSGO_DIR,
    HOST_COMPONENT,
    SERVER_EXECUTABLES,
    find_server,
    plugin_addon_dir,
    server_executable,
)
from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, run_tool
from voltmod.project import Project, Settings


def install_plugins(project: Project, server_path: str, plugin: str, preset: str) -> None:
    """Install the host and @p plugin, or every plugin when it is empty, into that server."""
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
    """Install the host: the only Metamod plugin, which loads every plugin beside it."""
    print("--- voltmod host ---")
    searched = _host_build_dirs(project, preset)
    for build_dir in searched:
        staging = _stage_component(build_dir, HOST_COMPONENT, required=False)
        if staging is None:
            continue
        _merge_addons(staging, csgo, HOST_COMPONENT)
        _print_staged(staging)
        return

    looked_in = "\n  ".join(str(path) for path in searched) or "(no build directory)"
    raise VoltmodError(
        f"no voltmod host was staged; looked in:\n  {looked_in}\n"
        f"Build the framework first: voltmod build {preset}"
    )


def _host_build_dirs(project: Project, preset: str) -> list[Path]:
    """This project's build tree, then an editable framework checkout's, which builds its own."""
    candidates = [project.build_dir(preset)]
    checkout = find_editable_framework()
    if checkout and checkout.resolve() != project.root.resolve():
        candidates.append(checkout / "build" / preset)
    return [path for path in candidates if path.is_dir()]


def _install_plugin(project: Project, name: str, csgo: Path, preset: str, *, named: bool) -> None:
    """Stage one plugin with `cmake --install`, then merge it into the server tree."""
    print(f"--- {name} ---")
    build_dir = project.build_dir(preset)
    if not build_dir.is_dir():
        if named:
            raise VoltmodError(f"no build at {build_dir}\nBuild first: voltmod build {preset}")
        print(f"  (skipped - no build at {build_dir})")
        return

    staging = _stage_component(build_dir, name, required=named)
    if staging is None:
        print(f"  (skipped - cmake --install produced nothing for {name})")
        return

    _merge_addons(staging, csgo, name)
    _print_staged(staging)
    _seed_settings(project, name, csgo)


def _seed_settings(project: Project, name: str, csgo: Path) -> None:
    """Copy the shipped settings once, so an operator's edits survive every later install."""
    plugin_dir = project.plugin_dir(name) or project.root / "plugins" / name
    source = plugin_dir / "configs/settings.jsonc"
    if not source.is_file():
        return
    target = csgo / plugin_addon_dir(name) / "configs/settings.jsonc"
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


def _stage_component(build_dir: Path, component: str, *, required: bool) -> Path | None:
    """Stage one install component under the build tree; None when it installs no addons."""
    staging = build_dir / "_install-staging" / component
    shutil.rmtree(staging, ignore_errors=True)
    try:
        run_tool(
            "cmake", "--install", str(build_dir), "--component", component,
            "--prefix", str(staging),
        )
        staged = (staging / "addons").is_dir()
    except subprocess.CalledProcessError:
        staged = False

    if staged:
        return staging
    if required:
        raise VoltmodError(f"cmake --install staged no addons/ for {component} (is it built?)")
    return None


def _merge_addons(staging: Path, csgo: Path, what: str) -> None:
    try:
        shutil.copytree(staging / "addons", csgo / "addons", dirs_exist_ok=True)
    except (shutil.Error, PermissionError) as error:
        raise VoltmodError(
            f"could not replace the installed files for {what}: {error}\n"
            "A running CS2 server holds the binary open; stop it and try again."
        ) from None
