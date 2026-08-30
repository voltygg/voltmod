# Conan replaces these class attributes at runtime, causing Pyright false positives.
# pyright: reportAttributeAccessIssue=false, reportCallIssue=false

import os
import shutil

from conan import ConanFile  # type: ignore[attr-defined]
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class VoltModConan(ConanFile):
    """Serve as both the repository's consumer recipe and VoltMod's package recipe.

    A checkout uses the output paths expected by the CMake presets. `conan create`
    uses `cmake_layout` and packages the headers, CMake helpers, gamedata, Panorama
    sources, and plugin
    template. Both modes resolve the same dependencies.
    """

    name = "voltmod"
    author = "Sukhrob Ilyosbekov (suxrobgm@gmail.com)"
    version = "1.3.0"
    description = "C++23 library for CS2 Metamod:Source plugins"
    license = "MIT"
    homepage = "https://github.com/voltygg/voltmod"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "static-library"

    options = {"with_postgres": [True, False]}

    # cpr is header-private. glaze is public through App/Config.hpp, never through Api.hpp.
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
        "panorama/*",
        "templates/plugin/*",
        "LICENSE",
    )

    def _source_checkout(self):
        # exports_sources omits CMakePresets.json, so this is false in the cache.
        return os.path.isfile(os.path.join(self.recipe_folder, "CMakePresets.json"))

    def requirements(self):
        self.requires("glaze/8.0.0", transitive_headers=True)
        self.requires("magic_enum/0.9.7", transitive_headers=True)
        self.requires("hl2sdk-cs2/[>=2026 <2028]",
                      transitive_headers=True, transitive_libs=True)
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
        # Checkouts use the paths defined by the public CMake presets.
        if self._source_checkout():
            self.folders.build = f"build/{self._preset()}"
            self.folders.generators = f"build/{self._preset()}/generators"
        else:
            cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        # Via the toolchain so `cmake --preset`, `conan build` and `conan create` all get it.
        if shutil.which("ccache"):
            toolchain.variables["CMAKE_CXX_COMPILER_LAUNCHER"] = "ccache"
        toolchain.variables["VOLTMOD_ENABLE_POSTGRES"] = bool(self.options.with_postgres)
        # hl2sdk-cs2's build module owns VOLTMOD_HL2SDK_DIR.
        if not self._source_checkout():
            toolchain.variables["BUILD_TESTING"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "voltmod")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::VoltMod")
        self.cpp_info.builddirs = ["cmake"]
        # Export the plugin and test helpers as CMakeDeps build modules.
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join("cmake", "VoltModPlugin.cmake"),
            os.path.join("cmake", "VoltModTests.cmake"),
        ])

        # Components match the CMake targets. An editable checkout's libraries sit in its
        # preset build tree, not lib/.
        libdirs = [self.folders.build] if self._source_checkout() else ["lib"]

        # All a test binary links.
        headers = self.cpp_info.components["headers"]
        headers.set_property("cmake_target_name", "VoltMod::Headers")
        headers.includedirs = ["include"]
        headers.requires = ["glaze::glaze", "magic_enum::magic_enum"]

        runtime = self.cpp_info.components["runtime"]
        runtime.set_property("cmake_target_name", "VoltMod::Runtime")
        runtime.libs = ["voltmod-runtime"]
        runtime.libdirs = libdirs
        runtime.requires = [
            "headers",
            "hl2sdk-cs2::hl2sdk-cs2",
            "metamod-source::metamod-source",
            "cpr::cpr",
        ]
        if self.settings.os == "Windows":
            runtime.system_libs = ["psapi"]

        if self.options.with_postgres:
            db = self.cpp_info.components["database"]
            db.set_property("cmake_target_name", "VoltMod::Database")
            db.libs = ["voltmod-database"]
            db.libdirs = libdirs
            db.requires = ["runtime", "libpqxx::libpqxx"]
            # Consumer feature checks and Database/Api.hpp's guard read this.
            db.defines = ["VOLTMOD_ENABLE_POSTGRES=1"]

        # voltmod_add_plugin links this component by default.
        umbrella = self.cpp_info.components["voltmod"]
        umbrella.set_property("cmake_target_name", "VoltMod::VoltMod")
        umbrella.requires = ["runtime"] + (["database"] if self.options.with_postgres else [])
