# Conan rebinds class attributes at runtime; ignore the Pyright false positives.
# pyright: reportAttributeAccessIssue=false, reportCallIssue=false

import os

# Sibling conan/ profiles dir shadows the conan package for mypy's file resolution.
from conan import ConanFile  # type: ignore[attr-defined]
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class VoltModConan(ConanFile):
    """voltmod: this repo's consumer conanfile AND its package recipe.

    Dependencies resolve identically either way - the SDKs are always Conan packages.
    The two differ only in build layout: a source checkout keeps the flat
    --output-folder the CMake presets expect, while `conan create` uses cmake_layout
    and ships the headers, cmake helpers (voltmod_add_plugin as a CMakeDeps build module),
    gamedata and the plugin template.
    """

    name = "voltmod"
    version = "1.2.1"
    description = "C++23 library for CS2 Metamod:Source plugins"
    license = "MIT"
    homepage = "https://github.com/voltygg/voltmod"
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
        # invalidating every published voltmod binary.
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
                "voltmod requires compiler.libcxx=libstdc++ (Valve's _GLIBCXX_USE_CXX11_ABI=0); "
                "use the shipped linux-steamrt profile "
                "(conan config install the repo's conan/ dir)")
        runtime = str(self.settings.get_safe("compiler.runtime"))
        if self.settings.os == "Windows" and runtime != "static":
            raise ConanInvalidConfiguration(
                "voltmod requires the static MSVC runtime (/MT); "
                "use the shipped windows-msvc profile")

    def _preset(self):
        """The CMake preset a checkout builds into. Preset names are public API."""
        toolchain = "windows-msvc" if self.settings.os == "Windows" else "linux-steamrt"
        return f"{toolchain}-{str(self.settings.build_type).lower()}"

    def layout(self):
        # A checkout keeps the flat --output-folder layout the CMake presets point at;
        # cmake_layout would derive a folder from build_type alone, which does not map onto
        # preset names that encode a toolchain. Spelling out where that build puts things is
        # also what makes `conan editable add` work: an editable consumer resolves the framework
        # through cpp.build/cpp.source rather than through a package folder.
        if self._source_checkout():
            self.folders.build = f"build/{self._preset()}"
            self.folders.generators = f"build/{self._preset()}/generators"
            # Per component, not just the top level: CMakeDeps reads the component
            # dirs, and those default to the package layout even in editable mode.
            for component in ("runtime", "database", "voltmod"):
                self.cpp.build.components[component].libdirs = ["."]
                self.cpp.source.components[component].includedirs = ["include"]
            self.cpp.build.libdirs = ["."]
            self.cpp.source.includedirs = ["include"]
            self.cpp.source.builddirs = ["cmake"]
        else:
            cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.variables["VOLTMOD_ENABLE_POSTGRES"] = bool(self.options.with_postgres)
        # VOLTMOD_HL2SDK_DIR is not set here - hl2sdk-cs2's own build module owns it.
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
        self.cpp_info.set_property("cmake_file_name", "voltmod")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::VoltMod")
        self.cpp_info.builddirs = ["cmake"]
        # voltmod_add_plugin / voltmod_add_tests reach consumers as CMakeDeps build
        # modules; VoltModPlugin.cmake pulls VoltModCommon itself.
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join("cmake", "VoltModPlugin.cmake"),
            os.path.join("cmake", "VoltModTests.cmake"),
        ])

        # Two components, matching the two libraries CMake builds. The source modules
        # are an architecture, not a packaging unit: nothing ever selected them
        # individually, so declaring them here only duplicated the CMake dependency
        # graph in a second place that could drift from it.
        runtime = self.cpp_info.components["runtime"]
        runtime.set_property("cmake_target_name", "VoltMod::Runtime")
        runtime.libs = ["voltmod-runtime"]
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
            db.set_property("cmake_target_name", "VoltMod::Database")
            db.libs = ["voltmod-database"]
            db.includedirs = ["include"]
            db.requires = ["runtime", "libpqxx::libpqxx"]
            # Consumer feature checks and Database/Api.hpp's #error guard read this.
            db.defines = ["VOLTMOD_ENABLE_POSTGRES=1"]

        # What voltmod_add_plugin links unless the plugin asks for a feature.
        umbrella = self.cpp_info.components["voltmod"]
        umbrella.set_property("cmake_target_name", "VoltMod::VoltMod")
        umbrella.requires = ["runtime"] + (["database"] if self.options.with_postgres else [])
