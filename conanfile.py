# Conan rebinds class attributes at runtime; ignore the Pyright false positives.
# pyright: reportAttributeAccessIssue=false, reportCallIssue=false

import os

# Sibling conan/ profiles dir shadows the conan package for mypy's file resolution.
from conan import ConanFile  # type: ignore[attr-defined]
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy


class CS2KitConan(ConanFile):
    """cs2-kit: consumer conanfile AND package recipe, mode-detected.

    Vendored mode (this repo checked out with its vendor/ submodules): plain
    dependency resolution for build.py's conan install - no layout, no SDK
    requires, CS2KitSdk.cmake finds the SDKs under vendor/.

    Package mode (`conan create` - exports_sources omits vendor/, so the
    exported recipe never sees the submodules): requires the hl2sdk-cs2 and
    metamod-source packages, builds libcs2-kit against them, and ships the headers,
    cmake helpers (cs2_add_plugin via CMakeDeps build modules), gamedata and the
    plugin template.
    """

    name = "cs2-kit"
    description = "C++23 library for CS2 Metamod:Source plugins"
    license = "MIT"
    homepage = "https://github.com/voltygg/cs2-kit"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "static-library"

    options = {"with_postgres": [True, False]}

    # cpr stays header-private; nlohmann is public surface (Api.hpp -> JsonConfig),
    # so it is declared in requirements() with transitive_headers.
    requires = ("cpr/1.11.2",)

    default_options = {
        "with_postgres": False,
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
    }

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "src/*",
        "gamedata/*",
        "templates/plugin/*",
        "LICENSE",
    )

    # Keep in sync with recipes/*/conandata.yml and the vendor/ submodule pins.
    HL2SDK_VERSION = "2026.07.23"
    METAMOD_VERSION = "2.0.0.20260711"

    def set_version(self):
        self.version = self.version or os.environ.get("CS2KIT_VERSION", "1.0.0")

    def _vendored_sdk(self):
        return os.path.isdir(os.path.join(self.recipe_folder, "vendor", "hl2sdk-cs2", "public"))

    def requirements(self):
        # transitive_headers: plugin TUs include SDK/json headers through Api.hpp;
        # transitive_libs: the plugin MODULE links the prebuilt SDK libs.
        self.requires("nlohmann_json/3.11.3", transitive_headers=True)
        if not self._vendored_sdk():
            self.requires(f"hl2sdk-cs2/{self.HL2SDK_VERSION}",
                          transitive_headers=True, transitive_libs=True)
            self.requires(f"metamod-source/{self.METAMOD_VERSION}", transitive_headers=True)
        if self.options.with_postgres:
            self.requires("libpqxx/7.10.0", transitive_headers=True, transitive_libs=True)

    def build_requirements(self):
        self.test_requires("doctest/2.5.2")

    def validate(self):
        check_min_cppstd(self, 23)
        if self.settings.os == "Linux" and self.settings.get_safe("compiler.libcxx") != "libstdc++":
            raise ConanInvalidConfiguration(
                "cs2-kit requires compiler.libcxx=libstdc++ (Valve's _GLIBCXX_USE_CXX11_ABI=0); "
                "use the shipped linux-steamrt profile (conan config install the repo's conan/ dir)")
        if self.settings.os == "Windows" and str(self.settings.get_safe("compiler.runtime")) != "static":
            raise ConanInvalidConfiguration(
                "cs2-kit requires the static MSVC runtime (/MT); "
                "use the shipped windows-msvc profile")

    def layout(self):
        # Vendored mode keeps the flat --output-folder layout build.py and the
        # CMake presets point at.
        if not self._vendored_sdk():
            cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.variables["CS2KIT_ENABLE_POSTGRES"] = bool(self.options.with_postgres)
        if not self._vendored_sdk():
            # Point CS2KitSdk.cmake's cache vars at the dependency packages
            # instead of the vendor/ submodules; it works unchanged either way.
            toolchain.variables["CS2KIT_HL2SDK_DIR"] = \
                self.dependencies["hl2sdk-cs2"].package_folder.replace("\\", "/")
            toolchain.variables["CS2KIT_MMSOURCE_DIR"] = \
                self.dependencies["metamod-source"].package_folder.replace("\\", "/")
            toolchain.variables["BUILD_TESTING"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        build, pkg = self.build_folder, self.package_folder
        copy(self, "*.a", build, os.path.join(pkg, "lib"), keep_path=False)
        copy(self, "*.lib", build, os.path.join(pkg, "lib"), keep_path=False)
        copy(self, "*.hpp", os.path.join(self.source_folder, "include"),
             os.path.join(pkg, "include"))
        # cmake/ next to gamedata/ so CS2KIT_ROOT_DIR (the cmake dir's parent)
        # resolves plugin.vdf.in, DoctestMain.cpp and gamedata as in the repo.
        copy(self, "*", os.path.join(self.source_folder, "cmake"), os.path.join(pkg, "cmake"))
        copy(self, "*", os.path.join(self.source_folder, "gamedata"),
             os.path.join(pkg, "gamedata"))
        copy(self, "*", os.path.join(self.source_folder, "templates", "plugin"),
             os.path.join(pkg, "templates", "plugin"))
        copy(self, "LICENSE", self.source_folder, os.path.join(pkg, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "cs2-kit")
        self.cpp_info.set_property("cmake_target_name", "CS2Kit::CS2Kit")
        self.cpp_info.libs = ["cs2-kit"]
        self.cpp_info.includedirs = ["include"]
        if self.options.with_postgres:
            self.cpp_info.defines = ["CS2KIT_ENABLE_POSTGRES=1"]
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["psapi"]
        self.cpp_info.builddirs = ["cmake"]
        # cs2_add_plugin / cs2_add_tests reach consumers as CMakeDeps build
        # modules; CS2Plugin.cmake pulls CS2KitSdk/CS2KitBuildInfo itself.
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join("cmake", "CS2Plugin.cmake"),
            os.path.join("cmake", "CS2Tests.cmake"),
        ])
