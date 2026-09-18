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
    uses `cmake_layout` and packages the host addon tree, the SDK and Database
    libraries, the headers, CMake helpers, gamedata, Panorama sources, and the
    plugin template. Both modes resolve the same dependencies.
    """

    name = "voltmod"
    author = "Sukhrob Ilyosbekov (suxrobgm@gmail.com)"
    version = "1.5.0"
    description = "C++23 library for CS2 Metamod:Source plugins"
    license = "MIT"
    homepage = "https://github.com/voltygg/voltmod"
    settings = "os", "compiler", "build_type", "arch"
    # What consumers link: the SDK. The package also ships the host module, which nothing links.
    package_type = "static-library"

    # cpr is header-private. glaze is public through App/Config.hpp, never through Api.hpp.
    requires = ("cpr/1.11.2",)

    default_options = {
        "*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
        "libpq/*:with_openssl": True,
        "mariadb-connector-c/*:with_curl": False,
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
        # All three connectors: the driver is chosen at runtime from config. Linking them
        # statically makes the LGPL MariaDB connector a relinkable-object obligation.
        self.requires("sqlpp23/0.70", transitive_headers=True, transitive_libs=True)

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
        # This recipe owns the version; the host stamps it into its build info.
        toolchain.variables["VOLTMOD_VERSION"] = self.version
        # Via the toolchain so `cmake --preset`, `conan build` and `conan create` all get it.
        if shutil.which("ccache"):
            toolchain.variables["CMAKE_CXX_COMPILER_LAUNCHER"] = "ccache"
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

    def _host_bin_dir(self):
        """Where the host module sits: the packaged addon tree, or a checkout's build output."""
        if self._source_checkout():
            arch = "windows-x86_64" if self.settings.os == "Windows" else "linux-x86_64"
            return os.path.join(self.folders.build, "host", arch)
        subdir = "win64" if self.settings.os == "Windows" else "linuxsteamrt64"
        return os.path.join("addons", "voltmod", "bin", subdir)

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

        sdk = self.cpp_info.components["sdk"]
        sdk.set_property("cmake_target_name", "VoltMod::Sdk")
        sdk.libs = ["voltmod-sdk"]
        sdk.libdirs = libdirs
        sdk.requires = [
            "headers",
            "hl2sdk-cs2::hl2sdk-cs2",
            "metamod-source::metamod-source",
            "cpr::cpr",
        ]
        if self.settings.os == "Windows":
            sdk.system_libs = ["psapi"]

        # Plugins link it only with FEATURES DATABASE.
        db = self.cpp_info.components["database"]
        db.set_property("cmake_target_name", "VoltMod::Database")
        db.libs = ["voltmod-database"]
        db.libdirs = libdirs
        db.requires = ["sdk", "sqlpp23::postgresql", "sqlpp23::mysql", "sqlpp23::sqlite3"]

        # The Metamod plugin that loads every other plugin. Nothing links it: it ships as the
        # server-ready addons/ tree that VoltModPlugin.cmake offers as the `host` component.
        host = self.cpp_info.components["host"]
        host.set_property("cmake_target_name", "VoltMod::Host")
        host.libs = []
        host.libdirs = []
        host.includedirs = []
        host.bindirs = [self._host_bin_dir()]

        # Every component, for a project that links the package without voltmod_add_plugin.
        umbrella = self.cpp_info.components["voltmod"]
        umbrella.set_property("cmake_target_name", "VoltMod::VoltMod")
        umbrella.requires = ["sdk", "database"]
