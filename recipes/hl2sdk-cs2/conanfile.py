import os
import shutil
import subprocess

from conan import ConanFile
from conan.errors import ConanException, ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git

PROTOBUF_SRC = "thirdparty/protobuf-3.21.8/src"


class Hl2SdkCs2Conan(ConanFile):
    name = "hl2sdk-cs2"
    description = ("HL2SDK (CS2 branch): headers, prebuilt libs, generated protobufs, "
                   "source-only TUs")
    license = "LicenseRef-Valve-Source-SDK"
    homepage = "https://github.com/alliedmodders/hl2sdk/tree/cs2"
    package_type = "static-library"
    # Prebuilt libraries and generated protobufs are toolchain-independent.
    settings = "os", "arch"
    exports = "cmake/hl2sdk-sources.cmake"
    no_copy_source = True

    HEADER_TREES = ["public", "game/shared", "game/server", "common"]

    # Shipped as sources only; hl2sdk-sources.cmake attaches the plugin group per module.
    ENGINE_SOURCES = [
        "entity2/entityidentity.cpp",
        "entity2/entitykeyvalues.cpp",
        "entity2/entitysystem.cpp",
        "tier1/keyvalues3.cpp",
        "tier1/rangecheckedvar.cpp",
        "tier1/utlbufferutil.cpp",
    ]
    PLUGIN_SOURCES = [
        "tier1/convar.cpp",
        "public/tier0/memoverride.cpp",
    ]

    PROTO_BATCHES = [
        {
            "out": "public",
            "paths": ["common", PROTOBUF_SRC],
            "protos": [
                "common/network_connection",
                "common/networkbasetypes",
                "common/engine_gcmessages",
                "common/valveextensions",
                "common/netmessages",
                "common/source2_steam_stats",  # imported by netmessages
            ],
        },
        {
            "out": "game-shared",
            # cs/ leads so cs_usercmd.pb.h lands flat beside usercmd.pb.h.
            "paths": ["game/shared/cs", "game/shared", "common", PROTOBUF_SRC],
            "protos": [
                "game/shared/usermessages",
                "game/shared/usercmd",
                "game/shared/gameevents",
                "game/shared/cs/cs_usercmd",
            ],
        },
    ]

    def set_version(self):
        pinned = list(self.conan_data["sources"])
        if len(pinned) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = pinned[0]

    def validate(self):
        if str(self.settings.os) not in ("Linux", "Windows") or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("hl2sdk-cs2 supports Linux/Windows x86_64 only")

    def source(self):
        pin = self.conan_data["sources"][self.version]
        Git(self).fetch_commit(url=pin["url"], commit=pin["commit"])

    def _protoc(self):
        relative = ("devtools/bin/protoc.exe" if self.settings.os == "Windows"
                    else "devtools/bin/linux/protoc")
        path = os.path.join(self.source_folder, relative)
        if not os.path.isfile(path):
            raise ConanException(f"the SDK checkout has no protoc at {relative}")
        if self.settings.os != "Windows":
            os.chmod(path, 0o755)  # Git may not preserve the executable bit.
        return path

    def build(self):
        protoc = self._protoc()
        for batch in self.PROTO_BATCHES:
            out_dir = os.path.join(self.build_folder, "generated", batch["out"])
            os.makedirs(out_dir, exist_ok=True)
            command = [
                protoc,
                *(f"--proto_path={os.path.join(self.source_folder, p)}" for p in batch["paths"]),
                f"--cpp_out={out_dir}",
                *(os.path.join(self.source_folder, f"{p}.proto") for p in batch["protos"]),
            ]
            subprocess.run(command, check=True)

    def package(self):
        src, dst = self.source_folder, self.package_folder
        for tree in self.HEADER_TREES:
            for pattern in ("*.h", "*.hpp", "*.inl", "*.inc", "*.proto"):
                copy(self, pattern, os.path.join(src, tree), os.path.join(dst, tree))

        for pattern in ("*.h", "*.inc", "*.proto"):
            copy(self, pattern, os.path.join(src, PROTOBUF_SRC), os.path.join(dst, PROTOBUF_SRC))

        for rel in self.ENGINE_SOURCES + self.PLUGIN_SOURCES:
            copy(self, os.path.basename(rel), os.path.join(src, os.path.dirname(rel)),
                 os.path.join(dst, os.path.dirname(rel)))

        copy(self, "*", os.path.join(self.build_folder, "generated"),
             os.path.join(dst, "generated"))
        self._package_libs()
        copy(self, "hl2sdk-sources.cmake", os.path.join(self.recipe_folder, "cmake"),
             os.path.join(dst, "cmake"))
        copy(self, "LICENSE*", src, os.path.join(dst, "licenses"))

    def _package_libs(self):
        src, dst = self.source_folder, self.package_folder

        if self.settings.os != "Linux":
            copy(self, "*", os.path.join(src, "lib/public/win64"),
                 os.path.join(dst, "lib/public/win64"))
            return

        lib_dir = os.path.join(dst, "lib/linux64")
        copy(self, "*", os.path.join(src, "lib/linux64"), lib_dir)

        # CMake needs lib-prefixed names; retain originals for path-based linking.
        for stem in ("mathlib", "interfaces"):
            plain = os.path.join(lib_dir, f"{stem}.a")
            if os.path.isfile(plain):
                shutil.copyfile(plain, os.path.join(lib_dir, f"lib{stem}.a"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "hl2sdk-cs2")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::HL2SDK")
        self.cpp_info.includedirs = [
            PROTOBUF_SRC,
            "public",
            "public/engine",
            "public/mathlib",
            "public/tier0",
            "public/tier1",
            "public/entity2",
            "game/shared",
            "game/server",
            "common",
            "generated/public",
            "generated/game-shared",
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
        self.cpp_info.set_property("cmake_build_modules", ["cmake/hl2sdk-sources.cmake"])
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
            # Keep this link order. GNU ld resolves static archives from left to right.
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
