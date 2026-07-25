# Conan rebinds class attributes at runtime; ignore the Pyright false positives.
# pyright: reportAttributeAccessIssue=false

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class ProjectConan(ConanFile):
    name = "$project"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "shared-library"

    requires = (
        "cpr/1.11.2",
        "nlohmann_json/3.11.3",
        # "libpqxx/7.10.0",  # uncomment together with CS2KIT_ENABLE_POSTGRES
    )

    default_options = {
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
    }

    def build_requirements(self):
        self.test_requires("doctest/2.5.2")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.generate()
