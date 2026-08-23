# Conan rebinds class attributes at runtime; ignore the Pyright false positives.
# pyright: reportAttributeAccessIssue=false, reportCallIssue=false

import os

# Sibling conan/ profiles dir shadows the conan package for mypy's file resolution.
from conan import ConanFile  # type: ignore[attr-defined]
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class CS2KitConan(ConanFile):
    """cs2-kit: this repo's consumer conanfile AND its package recipe.

    Dependencies resolve identically either way - the SDKs are always Conan packages.
    The two differ only in build layout: a source checkout keeps the flat
    --output-folder the CMake presets expect, while `conan create` uses cmake_layout
    and ships the headers, cmake helpers (cs2_add_plugin as a CMakeDeps build module),
    gamedata and the plugin template.
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

    def set_version(self):
        self.version = self.version or os.environ.get("CS2KIT_VERSION", "1.0.0")

    def _source_checkout(self):
        # exports_sources omits CMakePresets.json, so this is false in the cache.
        return os.path.isfile(os.path.join(self.recipe_folder, "CMakePresets.json"))

    def requirements(self):
        # transitive_headers: plugin TUs include SDK/json headers through Api.hpp;
        # transitive_libs: the plugin MODULE links the prebuilt SDK libs.
        self.requires("nlohmann_json/3.11.3", transitive_headers=True)
        # Ranges, so an SDK bump never edits this recipe; conan.lock pins the build.
        self.requires("hl2sdk-cs2/[>=2026 <2028]",
                      transitive_headers=True, transitive_libs=True)
        # Header-only, and it moves monthly - minor_mode keeps a bump from
        # invalidating every published cs2-kit binary.
        self.requires("metamod-source/[>=2.0 <3]",
                      transitive_headers=True, package_id_mode="minor_mode")
        if self.options.with_postgres:
            self.requires("libpqxx/7.10.0", transitive_headers=True, transitive_libs=True)

    def build_requirements(self):
        self.test_requires("doctest/2.5.2")

    def validate(self):
        check_min_cppstd(self, 23)
        if self.settings.os == "Linux" and self.settings.get_safe("compiler.libcxx") != "libstdc++":
            raise ConanInvalidConfiguration(
                "cs2-kit requires compiler.libcxx=libstdc++ (Valve's _GLIBCXX_USE_CXX11_ABI=0); "
                "use the shipped linux-steamrt profile "
                "(conan config install the repo's conan/ dir)")
        runtime = str(self.settings.get_safe("compiler.runtime"))
        if self.settings.os == "Windows" and runtime != "static":
            raise ConanInvalidConfiguration(
                "cs2-kit requires the static MSVC runtime (/MT); "
                "use the shipped windows-msvc profile")

    def layout(self):
        # A checkout keeps the flat --output-folder layout build.py and the CMake
        # presets point at; cmake_layout would derive a folder from build_type, which
        # does not map onto preset names that encode a toolchain.
        if not self._source_checkout():
            cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.variables["CS2KIT_ENABLE_POSTGRES"] = bool(self.options.with_postgres)
        # CS2KIT_HL2SDK_DIR is not set here - hl2sdk-cs2's own build module owns it.
        if not self._source_checkout():
            toolchain.variables["BUILD_TESTING"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        # CMake owns the output names and the install layout; this just runs it.
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "cs2-kit")
        self.cpp_info.set_property("cmake_target_name", "CS2Kit::CS2Kit")
        self.cpp_info.builddirs = ["cmake"]
        # cs2_add_plugin / cs2_add_tests reach consumers as CMakeDeps build
        # modules; CS2Plugin.cmake pulls CS2KitSdk/CS2KitBuildInfo itself.
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join("cmake", "CS2Plugin.cmake"),
            os.path.join("cmake", "CS2Tests.cmake"),
        ])

        # Two components, matching the two libraries CMake builds. The source modules
        # are an architecture, not a packaging unit: nothing ever selected them
        # individually, so declaring them here only duplicated the CMake dependency
        # graph in a second place that could drift from it.
        runtime = self.cpp_info.components["runtime"]
        runtime.set_property("cmake_target_name", "CS2Kit::Runtime")
        runtime.libs = ["cs2-kit-runtime"]
        runtime.includedirs = ["include"]
        runtime.requires = [
            "hl2sdk-cs2::hl2sdk-cs2",
            "metamod-source::metamod-source",
            "nlohmann_json::nlohmann_json",
            "cpr::cpr",
        ]
        if self.settings.os == "Windows":
            runtime.system_libs = ["psapi"]

        if self.options.with_postgres:
            db = self.cpp_info.components["database"]
            db.set_property("cmake_target_name", "CS2Kit::Database")
            db.libs = ["cs2-kit-database"]
            db.includedirs = ["include"]
            db.requires = ["runtime", "libpqxx::libpqxx"]
            # Consumer feature checks and Database/Api.hpp's #error guard read this.
            db.defines = ["CS2KIT_ENABLE_POSTGRES=1"]

        # What cs2_add_plugin links unless the plugin asks for a feature.
        umbrella = self.cpp_info.components["cs2-kit"]
        umbrella.set_property("cmake_target_name", "CS2Kit::CS2Kit")
        umbrella.requires = ["runtime"] + (["database"] if self.options.with_postgres else [])
