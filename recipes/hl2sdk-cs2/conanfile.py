import os
import shutil
import subprocess

import yaml
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git


class Hl2SdkCs2Conan(ConanFile):
    """Package the AlliedModders CS2 HL2SDK for VoltMod.

    The package preserves the upstream layout and includes headers, prebuilt Valve
    libraries, protobuf inputs and outputs, and consumer-compiled translation units.
    `cmake/hl2sdk-sources.cmake` attaches those units. `conandata.yml` pins the
    version and commit.
    """

    name = "hl2sdk-cs2"
    description = ("HL2SDK (CS2 branch): headers, prebuilt libs, generated protobufs, "
                   "source-only TUs")
    license = "LicenseRef-Valve-Source-SDK"
    homepage = "https://github.com/alliedmodders/hl2sdk/tree/cs2"
    package_type = "static-library"
    # Valve libraries have no compiler or build type. Generated protobuf sources are
    # identical across toolchains.
    settings = "os", "arch"
    exports = "cmake/hl2sdk-sources.cmake"

    HEADER_TREES = ["public", "game/shared", "game/server", "common"]
    PROTOBUF_SRC = "thirdparty/protobuf-3.21.8/src"
    # VoltMod compiles six files. Each plugin compiles convar.cpp for its own ConVar
    # state and memoverride.cpp for the global allocation operators.
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

    PROTO_BATCHES = [
        {
            "src": "common",
            "out": "public",
            "paths": ["common", PROTOBUF_SRC],
            # netmessages imports source2_steam_stats.
            "names": [
                "network_connection",
                "networkbasetypes",
                "engine_gcmessages",
                "valveextensions",
                "netmessages",
                "source2_steam_stats",
            ],
        },
        {
            "src": "game/shared",
            "out": "game-shared",
            "paths": ["common", "game/shared", PROTOBUF_SRC],
            "names": ["usermessages", "usercmd", "gameevents"],
        },
        {
            # Resolve cs_usercmd.proto flat so its header sits beside usercmd.pb.h.
            "src": "game/shared/cs",
            "out": "game-shared",
            "paths": ["game/shared/cs", "game/shared", "common", PROTOBUF_SRC],
            "names": ["cs_usercmd"],
        },
    ]

    def set_version(self):
        with open(os.path.join(self.recipe_folder, "conandata.yml"), encoding="utf-8") as handle:
            sources = yaml.safe_load(handle)["sources"]
        if len(sources) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = next(iter(sources))

    def validate(self):
        if str(self.settings.os) not in ("Linux", "Windows") or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("hl2sdk-cs2 supports Linux/Windows x86_64 only")

    def source(self):
        data = self.conan_data["sources"][self.version]
        Git(self).fetch_commit(url=data["url"], commit=data["commit"])

    def _protoc(self):
        windows = self.settings.os == "Windows"
        relative = "devtools/bin/protoc.exe" if windows else "devtools/bin/linux/protoc"
        path = os.path.join(self.source_folder, relative)
        if not os.path.isfile(path):
            raise ConanInvalidConfiguration(f"the SDK checkout has no protoc at {relative}")
        if self.settings.os != "Windows":
            os.chmod(path, 0o755)  # Git may not preserve the executable bit.
        return path

    def build(self):
        # Generate once here because the package pins both protoc and its inputs.
        protoc = self._protoc()
        for batch in self.PROTO_BATCHES:
            out_dir = os.path.join(self.build_folder, "generated", batch["out"])
            os.makedirs(out_dir, exist_ok=True)
            args = [f"--proto_path={os.path.join(self.source_folder, p)}" for p in batch["paths"]]
            for name in batch["names"]:
                proto = os.path.join(self.source_folder, batch["src"], f"{name}.proto")
                subprocess.run([protoc, *args, f"--cpp_out={out_dir}", proto], check=True)

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
        # Package the generated sources, but not the build-only protoc executable.
        copy(self, "*", os.path.join(self.build_folder, "generated"),
             os.path.join(dst, "generated"))
        if self.settings.os == "Linux":
            lib_dir = os.path.join(dst, "lib/linux64")
            copy(self, "*", os.path.join(src, "lib/linux64"), lib_dir)
            # Add the lib prefix required by CMake's Linux library search. Keep the
            # originals for path-based linking.
            for stem in ("mathlib", "interfaces"):
                plain = os.path.join(lib_dir, f"{stem}.a")
                if os.path.isfile(plain):
                    shutil.copyfile(plain, os.path.join(lib_dir, f"lib{stem}.a"))
        else:
            copy(self, "*", os.path.join(src, "lib/public/win64"),
                 os.path.join(dst, "lib/public/win64"))
        copy(self, "hl2sdk-sources.cmake", os.path.join(self.recipe_folder, "cmake"),
             os.path.join(dst, "cmake"))
        copy(self, "LICENSE*", src, os.path.join(dst, "licenses"))

    def package_info(self):
        # Keep all SDK usage requirements in this package.
        self.cpp_info.set_property("cmake_file_name", "hl2sdk-cs2")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::HL2SDK")
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
            os.path.join("generated", "public"),
            os.path.join("generated", "game-shared"),
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
        self.cpp_info.set_property("cmake_build_modules",
                                   [os.path.join("cmake", "hl2sdk-sources.cmake")])
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
            # Link order, not alphabetical: GNU ld resolves static archives left to right.
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
