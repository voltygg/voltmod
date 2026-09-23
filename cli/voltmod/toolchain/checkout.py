from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.project import Project
from voltmod.toolchain.conan import (
    PREBUILT_SDK_ARGS,
    conan_json,
    find_checkout,
    profile_args,
)
from voltmod.toolchain.msvc import load_msvc_environment
from voltmod.toolchain.process import run_tool, tool_output


def build_checkout(project: Project, checkout: Path, preset: str) -> None:
    """Compile the checkout into its own build/<preset>; only what changed recompiles."""
    load_msvc_environment()
    args = _checkout_args(project, checkout, preset)
    run_tool("conan", "build", *args, "--build=missing", *PREBUILT_SDK_ARGS)


def relock_framework(project: Project, preset: str) -> str:
    """Export the editable checkout as a package, pin it, drop the editable; return its folder."""
    checkout = find_checkout()
    if checkout is None:
        raise VoltmodError(
            "no editable voltmod checkout; register one with `conan editable add <path>`"
        )
    build_checkout(project, checkout, preset)
    run_tool("conan", "editable", "remove", checkout, check=False)

    # The lock pins only the recipe revision, so drop older binaries that could win over this one.
    reference = conan_json("export", checkout)["reference"]
    run_tool("conan", "remove", f"{reference}:*", "--confirm", check=False)
    exported = conan_json("export-pkg", *_checkout_args(project, checkout, preset))
    package_id = next(
        node["package_id"]
        for node in exported["graph"]["nodes"].values()
        if node["ref"].startswith("voltmod/")
    )
    package_folder = tool_output("conan", "cache", "path", f"{reference}:{package_id}").strip()

    _pin_framework(project, preset)
    return package_folder


def check_build_uses_package(project: Project, preset: str, package_folder: str) -> None:
    """Fail unless the kept build tree was reconfigured against the relocked package."""
    generators = project.build_dir(preset) / "generators"
    expected = Path(package_folder).resolve().as_posix().lower()
    for data in generators.glob("voltmod-*-data.cmake"):
        if expected in data.read_text(encoding="utf-8").replace("\\", "/").lower():
            return
    raise VoltmodError(
        f"build/{preset} is not configured against {package_folder}; delete it and rebuild"
    )


def _checkout_args(project: Project, checkout: Path, preset: str) -> list[str | Path]:
    # The consumer's lock: dependency versions are part of the package id the plugins resolve.
    lock = project.lockfile
    lock_args = [f"--lockfile={lock}", "--lockfile-partial"] if lock.is_file() else []
    return [checkout, *profile_args(checkout, preset), *lock_args]


def _pin_framework(project: Project, preset: str) -> None:
    """Re-pin voltmod in conan.lock to the newest revision in the local cache."""
    lock = project.lockfile
    lock_args: list[str] = []
    if lock.is_file():
        lock_args = [f"--lockfile={lock}"]
        # `--update` never re-pins a revision the lock already names, so the entry goes first.
        run_tool(
            "conan", "lock", "remove", "--requires=voltmod/*", *lock_args, f"--lockfile-out={lock}"
        )
    profile = profile_args(project.root, preset)
    lock_out = f"--lockfile-out={lock}"
    run_tool("conan", "lock", "create", project.root, *profile, *lock_args, lock_out, "--no-remote")
