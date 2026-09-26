# pyright: reportAttributeAccessIssue=false, reportOptionalCall=false, reportOptionalMemberAccess=false

import shutil
from typing import Any

from conan import ConanFile  # type: ignore[attr-defined]
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain


class VoltModConan(ConanFile):
    name = "voltmod"
    author = "Sukhrob Ilyosbekov (suxrobgm@gmail.com)"
    version = "1.7.0"
    description = "C++23 framework for CS2 server plugins"
    license = "MIT"
    homepage = "https://github.com/voltygg/voltmod"
    settings: Any = "os", "compiler", "build_type", "arch"
    package_type = "static-library"

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

    def requirements(self) -> None:
        self.requires("cpr/1.11.2")
        self.requires("glaze/8.0.0", transitive_headers=True)
        self.requires("magic_enum/0.9.7", transitive_headers=True)
        self.requires("hl2sdk-cs2/[>=2026 <2028]", transitive_headers=True, transitive_libs=True)
        self.requires("khook/[>=2026 <2028]", transitive_headers=True)
        # All three connectors: the driver is chosen at runtime from config. Linking them
        # statically makes the LGPL MariaDB connector a relinkable-object obligation.
        self.requires("sqlpp23/0.70", transitive_headers=True, transitive_libs=True)

    def build_requirements(self) -> None:
        self.test_requires("doctest/2.5.2")

    def validate(self) -> None:
        check_min_cppstd(self, 23)
        if self.settings.os == "Linux" and self.settings.get_safe("compiler.libcxx") != "libstdc++":
            raise ConanInvalidConfiguration(
                "voltmod requires compiler.libcxx=libstdc++ (Valve's _GLIBCXX_USE_CXX11_ABI=0); "
                "use the shipped linux-steamrt profile "
                "(conan config install the repo's conan/ dir)"
            )
        runtime = str(self.settings.get_safe("compiler.runtime"))
        if self.settings.os == "Windows" and runtime != "static":
            raise ConanInvalidConfiguration(
                "voltmod requires the static MSVC runtime (/MT); "
                "use the shipped windows-msvc profile"
            )

    def _preset(self) -> str:
        """The CMake preset a checkout builds into. Preset names are public API."""
        toolchain = "windows-msvc" if self.settings.os == "Windows" else "linux-steamrt"
        return f"{toolchain}-{str(self.settings.build_type).lower()}"

    def layout(self) -> None:
        # The public CMake presets' build tree, for a checkout and the cache alike.
        build = f"build/{self._preset()}"
        self.folders.build = build
        self.folders.generators = f"{build}/generators"
        # An editable checkout's libraries sit at the top of its build tree, not in lib/.
        for component in ("portable", "sdk", "database"):
            self.cpp.build.components[component].libdirs = ["."]

    def generate(self) -> None:
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        # This recipe owns the version; the host reports it in its load line.
        toolchain.variables["VOLTMOD_VERSION"] = self.version
        # Via the toolchain so `cmake --preset`, `conan build` and `conan create` all get it.
        if shutil.which("ccache"):
            toolchain.variables["CMAKE_CXX_COMPILER_LAUNCHER"] = "ccache"
        toolchain.generate()

    def build(self) -> None:
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self) -> None:
        cmake = CMake(self)
        cmake.install()

    def package_info(self) -> None:
        self.cpp_info.set_property("cmake_file_name", "voltmod")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::VoltMod")
        self.cpp_info.builddirs = ["cmake"]
        # Export the plugin and test helpers as CMakeDeps build modules.
        self.cpp_info.set_property(
            "cmake_build_modules",
            [
                "cmake/VoltModPlugin.cmake",
                "cmake/VoltModTests.cmake",
            ],
        )

        # Components match the CMake targets. CMakeDeps makes VoltMod::VoltMod link them all.
        portable = self.cpp_info.components["portable"]
        portable.set_property("cmake_target_name", "VoltMod::Portable")
        portable.libs = ["voltmod-portable"]
        portable.requires = ["glaze::glaze", "magic_enum::magic_enum"]
        if self.settings.os == "Windows":
            portable.system_libs = ["psapi"]

        sdk = self.cpp_info.components["sdk"]
        sdk.set_property("cmake_target_name", "VoltMod::Sdk")
        sdk.libs = ["voltmod-sdk"]
        sdk.requires = [
            "portable",
            "hl2sdk-cs2::hl2sdk-cs2",
            "khook::headers",
            "cpr::cpr",
        ]

        # Plugins link it only with DATABASE.
        database = self.cpp_info.components["database"]
        database.set_property("cmake_target_name", "VoltMod::Database")
        database.libs = ["voltmod-database"]
        # fmt: off
        database.requires = [
            "portable", "sqlpp23::postgresql", "sqlpp23::mysql", "sqlpp23::sqlite3",
        ]
        # fmt: on
        if self.settings.os == "Windows":
            # The MariaDB connector needs winsock2.h before the windows.h other headers pull in.
            database.defines = ["NOMINMAX", "WIN32_LEAN_AND_MEAN"]
