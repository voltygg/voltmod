# Conan rebinds class attributes at runtime; ignore the Pyright false positives.
# pyright: reportAttributeAccessIssue=false

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class ProjectConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    # cpr and nlohmann_json arrive transitively through voltmod.
    requires = ("voltmod/[~1]",)

    default_options = {
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
        # Postgres support: flip to True (pulls libpqxx transitively).
        "voltmod/*:with_postgres": False,
    }

    def build_requirements(self):
        # For voltmod_add_tests targets; drop if the project has no unit tests.
        self.test_requires("doctest/2.5.2")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.generate()
