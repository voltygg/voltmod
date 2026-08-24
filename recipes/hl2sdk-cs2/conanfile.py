import os
import shutil
import subprocess

import yaml
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git


class Hl2SdkCs2Conan(ConanFile):
    """AlliedModders HL2SDK, CS2 branch, trimmed for voltmod consumption.

    The tree mirrors the upstream layout. Ships headers, the prebuilt Valve libs, the
    .proto files and the sources generated from them, and the TUs consumers compile
    themselves - which cmake/hl2sdk-sources.cmake attaches by function, so no consumer
    reproduces this layout.

    conandata.yml is the sole source of truth for the version and the commit it pins.
    """

    name = "hl2sdk-cs2"
    description = ("HL2SDK (CS2 branch): headers, prebuilt libs, generated protobufs, "
                   "source-only TUs")
    license = "LicenseRef-Valve-Source-SDK"
    homepage = "https://github.com/alliedmodders/hl2sdk/tree/cs2"
    package_type = "static-library"
    # No compiler/build_type: the prebuilt Valve libs have neither, and generated protobuf
    # *source* is identical wherever protoc runs.
    settings = "os", "arch"
    exports = "cmake/hl2sdk-sources.cmake"

    HEADER_TREES = ["public", "game/shared", "game/server", "common"]
    PROTOBUF_SRC = "thirdparty/protobuf-3.21.8/src"
    # Bodies compiled from source: six into the voltmod lib, plus convar.cpp and
    # memoverride.cpp per plugin (own ConVar state, global operator new/delete).
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

    # Each batch generates `names` from `src` into `out`, resolving imports along `paths`.
    PROTO_BATCHES = [
        {
            "src": "common",
            "out": "public",
            "paths": ["common", PROTOBUF_SRC],
            # netmessages carries the engine<->client control messages; source2_steam_stats
            # is one of its imports, so it comes along.
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
            # game/shared/cs leads so cs_usercmd.proto resolves flat, putting its header
            # beside the usercmd.pb.h it includes.
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
            os.chmod(path, 0o755)  # git does not always preserve the bit
        return path

    def build(self):
        # Here rather than in every consumer's configure: the output is a pure function of
        # the .proto files and this protoc, both pinned by this package.
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
        # Generated .pb.h/.pb.cc. protoc itself is not shipped - only build() needs it.
        copy(self, "*", os.path.join(self.build_folder, "generated"),
             os.path.join(dst, "generated"))
        if self.settings.os == "Linux":
            lib_dir = os.path.join(dst, "lib/linux64")
            copy(self, "*", os.path.join(src, "lib/linux64"), lib_dir)
            # Valve omits the lib prefix and CMake's find_library only searches lib* on
            # Linux. Add the conventional spelling; the originals stay for path linking.
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
        # The only definition of the SDK's usage requirements; nothing mirrors it.
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
