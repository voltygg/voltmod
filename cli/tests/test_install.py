"""Installing the host and a plugin into a local CS2 server."""

from pathlib import Path

import pytest

from voltmod.errors import VoltmodError
from voltmod.platforms import Platform
from voltmod.project import Project
from voltmod.server import install
from voltmod.server.cs2_server import CSGO_DIR, Cs2Server
from voltmod.server.install import HOST_GAMEDATA, HOST_VDF, host_binary, plugin_dir

PRESET = "windows-msvc-release"
HOST_DLL = host_binary(Platform.WINDOWS)
DEMO = plugin_dir("demo")

HOST_FILES = (HOST_VDF, HOST_DLL, HOST_GAMEDATA)
PLUGIN_FILES = (f"{DEMO}/demo.dll", f"{DEMO}/plugin.json")


@pytest.fixture(autouse=True)
def no_editable_framework(monkeypatch: pytest.MonkeyPatch) -> None:
    """Whatever this machine has registered with `conan editable` must not reach a test."""
    monkeypatch.setattr(install, "linked_checkout", lambda root: None)


@pytest.fixture
def cs2_server(tmp_path: Path) -> Path:
    (tmp_path / "server" / CSGO_DIR).mkdir(parents=True)
    return tmp_path / "server"


@pytest.fixture
def project(tmp_path: Path) -> Project:
    """A consumer repo with one plugin, its settings file, and a build directory."""
    configs = tmp_path / "repo/plugins/demo/configs"
    configs.mkdir(parents=True)
    (configs / "settings.jsonc").write_text("{ shipped: true }", encoding="utf-8")
    (tmp_path / "repo/build" / PRESET).mkdir(parents=True)
    return Project(tmp_path / "repo")


Staged = dict[tuple[Path, str], tuple[str, ...]]


def stage_components(monkeypatch: pytest.MonkeyPatch, staged: Staged) -> None:
    """Replace cmake with a stub writing the files each (build directory, component) installs."""

    def run_tool(tool: str, *args: str, **_: object) -> None:
        assert tool == "cmake"
        build_dir = Path(args[1])
        component = args[args.index("--component") + 1]
        prefix = Path(args[args.index("--prefix") + 1])
        for relative in staged.get((build_dir, component), ()):
            target = prefix / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(component, encoding="utf-8")

    monkeypatch.setattr(install, "run_tool", run_tool)


def test_seeded_settings_survive_a_reinstall(
    project: Project, cs2_server: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = project.build_dir(PRESET)
    stage_components(monkeypatch, {(build, "host"): HOST_FILES, (build, "demo"): PLUGIN_FILES})

    install.install_plugins(project, Cs2Server(cs2_server), ["demo"], PRESET)
    settings = cs2_server / CSGO_DIR / DEMO / "configs/settings.jsonc"
    assert settings.read_text(encoding="utf-8") == "{ shipped: true }"

    settings.write_text("{ edited: true }", encoding="utf-8")
    install.install_plugins(project, Cs2Server(cs2_server), ["demo"], PRESET)
    assert settings.read_text(encoding="utf-8") == "{ edited: true }"


def test_the_host_comes_from_an_editable_framework_checkout(
    project: Project, cs2_server: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    checkout = project.root.parent / "voltmod"
    framework_build = checkout / "build" / PRESET
    framework_build.mkdir(parents=True)
    monkeypatch.setattr(install, "linked_checkout", lambda root: checkout)
    stage_components(
        monkeypatch,
        {(framework_build, "host"): HOST_FILES, (project.build_dir(PRESET), "demo"): PLUGIN_FILES},
    )

    install.install_plugins(project, Cs2Server(cs2_server), ["demo"], PRESET)

    assert (cs2_server / CSGO_DIR / HOST_DLL).is_file()


def test_a_missing_host_is_an_error(
    project: Project, cs2_server: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stage_components(monkeypatch, {(project.build_dir(PRESET), "demo"): PLUGIN_FILES})

    with pytest.raises(VoltmodError, match="no voltmod host"):
        install.install_plugins(project, Cs2Server(cs2_server), ["demo"], PRESET)
