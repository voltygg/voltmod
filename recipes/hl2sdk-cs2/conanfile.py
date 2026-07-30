import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git


class Hl2SdkCs2Conan(ConanFile):
    """AlliedModders HL2SDK, CS2 branch, trimmed for cs2-kit consumption.

    The package tree mirrors the upstream layout (public/, game/, common/,
    lib/, devtools/), so cs2-kit's CS2KitSdk.cmake works against it unchanged:
    the shipped hl2sdk-vars.cmake build module force-points CS2KIT_HL2SDK_DIR
    at the package folder. Ships headers, the prebuilt Valve libs, protoc, the
    .proto files, and the handful of source-only TUs cs2-kit compiles
    (convar.cpp and memoverride.cpp are compiled per plugin by cs2_add_plugin).

    Valve's Source SDK license does not permit public redistribution - keep
    this package on a private remote. Includes/defines/libs in package_info
    must stay in sync with cmake/CS2KitSdk.cmake (submodule mode).
    """

    name = "hl2sdk-cs2"
    description = "HL2SDK (CS2 branch): headers, prebuilt libs, protoc, source-only TUs"
    license = "LicenseRef-Valve-Source-SDK"
    homepage = "https://github.com/alliedmodders/hl2sdk/tree/cs2"
    package_type = "static-library"
    # Deliberately no compiler/build_type: one binary package per OS, exactly
    # how the prebuilt SDK libs are consumed in submodule mode.
    settings = "os", "arch"
    exports = "cmake/hl2sdk-vars.cmake"
    no_copy_source = True

    HEADER_TREES = ["public", "game/shared", "game/server", "common"]
    PROTOBUF_SRC = "thirdparty/protobuf-3.21.8/src"
    # Bodies consumed as sources: six in the cs2-kit static lib, plus
    # convar.cpp/memoverride.cpp recompiled per plugin (own ConVar state,
    # global operator new/delete).
    SOURCE_ONLY = [
        "entity2/entityidentity.cpp",
        "entity2/entitykeyvalues.cpp",
        "entity2/entitysystem.cpp",
        "tier1/keyvalues3.cpp",
        "tier1/rangecheckedvar.cpp",
        "tier1/utlbufferutil.cpp",
        "tier1/convar.cpp",
        "public/tier0/memoverride.cpp",
    ]

    def validate(self):
        if str(self.settings.os) not in ("Linux", "Windows") or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("hl2sdk-cs2 supports Linux/Windows x86_64 only")

    def source(self):
        data = self.conan_data["sources"][self.version]
        Git(self).fetch_commit(url=data["url"], commit=data["commit"])

    def package(self):
        src, dst = self.source_folder, self.package_folder
        for tree in self.HEADER_TREES:
            for pattern in ("*.h", "*.hpp", "*.inl", "*.inc", "*.proto"):
                copy(self, pattern, os.path.join(src, tree), os.path.join(dst, tree))
        for pattern in ("*.h", "*.inc", "*.proto"):
            copy(self, pattern, os.path.join(src, self.PROTOBUF_SRC),
                 os.path.join(dst, self.PROTOBUF_SRC))
        for rel in self.SOURCE_ONLY:
            copy(self, os.path.basename(rel), os.path.join(src, os.path.dirname(rel)),
                 os.path.join(dst, os.path.dirname(rel)))
        if self.settings.os == "Linux":
            copy(self, "*", os.path.join(src, "lib/linux64"), os.path.join(dst, "lib/linux64"))
            copy(self, "protoc", os.path.join(src, "devtools/bin/linux"),
                 os.path.join(dst, "devtools/bin/linux"))
        else:
            copy(self, "*", os.path.join(src, "lib/public/win64"), os.path.join(dst, "lib/public/win64"))
            copy(self, "protoc.exe", os.path.join(src, "devtools/bin"), os.path.join(dst, "devtools/bin"))
        copy(self, "hl2sdk-vars.cmake", os.path.join(self.recipe_folder, "cmake"),
             os.path.join(dst, "cmake"))
        copy(self, "LICENSE*", src, os.path.join(dst, "licenses"))

    def package_info(self):
        # Keep in sync with cmake/CS2KitSdk.cmake (cs2kit_configure_sdk).
        self.cpp_info.set_property("cmake_file_name", "hl2sdk-cs2")
        self.cpp_info.set_property("cmake_target_name", "CS2Kit::HL2SDK")
        self.cpp_info.includedirs = [
            self.PROTOBUF_SRC,
            "public",
            "public/engine",
            "public/mathlib",
            "public/tier0",
            "public/tier1",
            "public/entity2",
            "game/shared",
            "game/server",
            "common",
        ]
        self.cpp_info.defines = [
            "SOURCE_ENGINE=25",
            "GAME_DLL",
            "RAD_TELEMETRY_DISABLED",
            "META_IS_SOURCE2",
            "X64BITS",
            "PLATFORM_64BITS",
        ]
        self.cpp_info.builddirs = ["cmake"]
        self.cpp_info.set_property("cmake_build_modules", [os.path.join("cmake", "hl2sdk-vars.cmake")])
        if self.settings.os == "Linux":
            self.cpp_info.defines += [
                "stricmp=strcasecmp",
                "_stricmp=strcasecmp",
                "_snprintf=snprintf",
                "_vsnprintf=vsnprintf",
                "HAVE_STDINT_H",
                "GNUC",
                "COMPILER_GCC",
                "LINUX",
                "_LINUX",
                "POSIX",
                "_FILE_OFFSET_BITS=64",
                "_GLIBCXX_USE_CXX11_ABI=0",
            ]
            self.cpp_info.cxxflags = ["-fno-strict-aliasing"]
            self.cpp_info.libdirs = ["lib/linux64", "lib/linux64/release"]
            self.cpp_info.libs = ["mathlib", "interfaces", "protobuf", "tier0"]
            self.cpp_info.system_libs = ["m"]
            self.cpp_info.sharedlinkflags = ["-static-libstdc++", "-static-libgcc"]
            self.cpp_info.exelinkflags = ["-static-libstdc++", "-static-libgcc"]
        else:
            self.cpp_info.defines += [
                "WIN32",
                "WIN64",
                "_WINDOWS",
                "COMPILER_MSVC",
                "COMPILER_MSVC64",
                "_CRT_SECURE_NO_DEPRECATE",
                "_CRT_SECURE_NO_WARNINGS",
                "_CRT_NONSTDC_NO_DEPRECATE",
                "NOMINMAX",
            ]
            self.cpp_info.cxxflags = ["/utf-8"]
            self.cpp_info.libdirs = ["lib/public/win64", "lib/public/win64/2015"]
            self.cpp_info.libs = ["libprotobuf", "mathlib", "tier0", "interfaces"]
            self.cpp_info.system_libs = ["legacy_stdio_definitions"]
