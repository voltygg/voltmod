"""Installing plugins into a local CS2 server and running it; fleet deployment is the consumer's."""

import shutil
import subprocess
from pathlib import Path

from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.platforms import Platform
from voltmod.project import Plugin, Project
from voltmod.server.cs2_server import CSGO_DIR, Cs2Server
from voltmod.toolchain.conan import editable_framework
from voltmod.toolchain.process import run_tool

# The host is the only Metamod plugin: one per server, loading modules from its plugins directory.
# Paths below are relative to the game directory.
HOST_COMPONENT = "host"
HOST_ADDON_DIR = "addons/voltmod"
PLUGINS_DIR = f"{HOST_ADDON_DIR}/plugins"
HOST_VDF = "addons/metamod/voltmod.vdf"
HOST_GAMEDATA = f"{HOST_ADDON_DIR}/gamedata/gamedata.jsonc"

# Written by a server running voltmod once a map runs; relative to the install root.
SCHEMA_DUMP = f"{CSGO_DIR}/{HOST_ADDON_DIR}/schema/server.json"


def host_binary(platform: Platform) -> str:
    return f"{HOST_ADDON_DIR}/bin/{platform.bin_dir}/voltmod{platform.library_suffix}"


def plugin_dir(name: str) -> str:
    """Where one plugin's files live, relative to the game directory."""
    return f"{PLUGINS_DIR}/{name}"


def install_plugins(project: Project, server: Cs2Server, names: list[str], preset: str) -> None:
    """Install the host and the named plugins, or every plugin when none is named."""
    csgo = server.csgo
    plugins = project.installable_plugins(names)

    console.step(f"Installing into {server.root} from build/{preset}")

    _install_host(project, csgo, preset)
    for plugin in plugins:
        _install_plugin(project, plugin, csgo, preset, named=bool(names))

    installed = " ".join(plugin.name for plugin in plugins)
    console.done(f"Installed {installed}")
    console.info("Verify on the server console with: volt list")


def _install_host(project: Project, csgo: Path, preset: str) -> None:
    """Install the host: the only Metamod plugin, which loads its managed plugins."""
    console.section("voltmod host")
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
        f"Build the framework first: voltmod build -p {preset}"
    )


def _install_plugin(
    project: Project, plugin: Plugin, csgo: Path, preset: str, *, named: bool
) -> None:
    """Stage one plugin with `cmake --install`, then merge it into the server tree.

    A plugin asked for by name must install; a bare install skips what is not built.
    """
    console.section(plugin.name)
    staging = _stage_component(project.build_dir(preset), plugin.name)
    if staging is None:
        if named:
            raise VoltmodError(f"{plugin.name} is not built; run `voltmod build -p {preset}` first")
        console.note(f"skipped: not built for {preset}")
        return

    _merge_addons(staging, csgo, plugin.name)
    _print_staged(staging)
    _seed_settings(plugin, csgo)


def _seed_settings(plugin: Plugin, csgo: Path) -> None:
    """Copy the shipped settings once, so an operator's edits survive every later install."""
    source = plugin.dir / "configs/settings.jsonc"
    if not source.is_file():
        return
    target = csgo / plugin_dir(plugin.name) / "configs/settings.jsonc"
    if target.is_file():
        console.item("configs/settings.jsonc (kept the server's copy)")
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    console.item("configs/settings.jsonc (copied)")


def _print_staged(staging: Path) -> None:
    """Name what was merged, using the staged tree's own top-level paths."""
    top = sorted({path.relative_to(staging).parts[:2] for path in staging.rglob("*")})
    for parts in top:
        console.item("/".join(parts))


def _stage_component(build_dir: Path, component: str) -> Path | None:
    """Stage one install component under the build tree; None when it installs no addons."""
    if not build_dir.is_dir():
        return None
    staging = build_dir / "_install-staging" / component
    shutil.rmtree(staging, ignore_errors=True)
    try:
        run_tool("cmake", "--install", build_dir, "--component", component, "--prefix", staging)
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
