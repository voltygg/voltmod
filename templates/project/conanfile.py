# Conan replaces these class attributes at runtime, causing Pyright false positives.
# pyright: reportAttributeAccessIssue=false

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class ProjectConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    # cpr and glaze arrive transitively through voltmod.
    requires = ("voltmod/[~1]",)

    default_options = {
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
    }

    def build_requirements(self):
        # voltmod_add_tests uses doctest. Remove this if the project has no unit tests.
        self.test_requires("doctest/2.5.2")

    def layout(self):
        # The build tree the CMake presets expect: build/<preset>, generators beneath it.
        toolchain = "windows-msvc" if self.settings.os == "Windows" else "linux-steamrt"
        preset = f"{toolchain}-{str(self.settings.build_type).lower()}"
        self.folders.build = f"build/{preset}"
        self.folders.generators = f"build/{preset}/generators"

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.generate()
