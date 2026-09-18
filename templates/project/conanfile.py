# pyright: reportAttributeAccessIssue=false, reportOptionalCall=false

from typing import Any

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class ProjectConan(ConanFile):
    settings: Any = "os", "compiler", "build_type", "arch"
    requires = "voltmod/[~1]"

    default_options = {
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
    }

    def build_requirements(self) -> None:
        # voltmod_add_tests uses doctest. Remove this if the project has no unit tests.
        self.test_requires("doctest/2.5.2")

    def layout(self) -> None:
        # The build tree the CMake presets expect: build/<preset>, generators beneath it.
        toolchain = "windows-msvc" if self.settings.os == "Windows" else "linux-steamrt"
        build = f"build/{toolchain}-{str(self.settings.build_type).lower()}"
        self.folders.build = build
        self.folders.generators = f"{build}/generators"

    def generate(self) -> None:
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.generate()
