# Conan rebinds class attributes at runtime; ignore the Pyright false positives.
# pyright: reportAttributeAccessIssue=false, reportCallIssue=false

# Sibling conan/ profiles dir shadows the conan package for mypy's file resolution.
from conan import ConanFile  # type: ignore[attr-defined]
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class CS2KitConan(ConanFile):
    name = "cs2-kit"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "static-library"

    # Standalone kit development only; monorepo consumers declare libpqxx in their own conanfile.
    options = {"with_postgres": [True, False]}

    requires = (
        "cpr/1.11.2",
        "nlohmann_json/3.11.3",
    )

    default_options = {
        "with_postgres": False,
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
    }

    def requirements(self):
        if self.options.with_postgres:
            self.requires("libpqxx/7.10.0")

    def build_requirements(self):
        self.test_requires("doctest/2.5.2")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.variables["CS2KIT_ENABLE_POSTGRES"] = bool(self.options.with_postgres)
        toolchain.generate()
