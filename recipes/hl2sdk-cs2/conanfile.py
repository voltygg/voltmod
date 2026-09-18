# pyright: reportOptionalSubscript=false

import os
import shutil
import subprocess
from pathlib import Path
from typing import Any, NamedTuple

from conan import ConanFile
from conan.errors import ConanException, ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git

PROTOBUF_SRC = "thirdparty/protobuf-3.21.8/src"


class ProtoBatch(NamedTuple):
    out: str
    paths: tuple[str, ...]
    protos: tuple[str, ...]


class Hl2SdkCs2Conan(ConanFile):
    name = "hl2sdk-cs2"
    description = (
        "HL2SDK (CS2 branch): headers, prebuilt libs, generated protobufs, source-only TUs"
    )
    license = "LicenseRef-Valve-Source-SDK"
    homepage = "https://github.com/alliedmodders/hl2sdk/tree/cs2"
    package_type = "static-library"
    settings: Any = "os", "arch"
    exports = "cmake/hl2sdk-sources.cmake"
    no_copy_source = True

    HEADER_TREES = ("public", "game/shared", "game/server", "common")

    # Shipped as sources only; hl2sdk-sources.cmake attaches the plugin group per module.
    ENGINE_SOURCES = (
        "entity2/entityidentity.cpp",
        "entity2/entitykeyvalues.cpp",
        "entity2/entitysystem.cpp",
        "tier1/keyvalues3.cpp",
        "tier1/rangecheckedvar.cpp",
        "tier1/utlbufferutil.cpp",
    )
    PLUGIN_SOURCES = (
        "tier1/convar.cpp",
        "public/tier0/memoverride.cpp",
    )

    PROTO_BATCHES = (
        ProtoBatch(
            out="public",
            paths=("common", PROTOBUF_SRC),
            protos=(
                "common/network_connection",
                "common/networkbasetypes",
                "common/engine_gcmessages",
                "common/valveextensions",
                "common/netmessages",
                "common/source2_steam_stats",  # imported by netmessages
            ),
        ),
        ProtoBatch(
            out="game-shared",
            # cs/ leads so cs_usercmd.pb.h lands flat beside usercmd.pb.h.
            paths=("game/shared/cs", "game/shared", "common", PROTOBUF_SRC),
            protos=(
                "game/shared/usermessages",
                "game/shared/usercmd",
                "game/shared/gameevents",
                "game/shared/cs/cs_usercmd",
            ),
        ),
    )

    def set_version(self) -> None:
        pinned = list(self.conan_data["sources"])
        if len(pinned) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = pinned[0]

    def validate(self) -> None:
        if self.settings.os not in ("Linux", "Windows") or self.settings.arch != "x86_64":
            raise ConanInvalidConfiguration("hl2sdk-cs2 supports Linux/Windows x86_64 only")

    def source(self) -> None:
        pin = self.conan_data["sources"][self.version]
        Git(self).fetch_commit(url=pin["url"], commit=pin["commit"])

    def _protoc(self) -> Path:
        windows = self.settings.os == "Windows"
        relative = "devtools/bin/protoc.exe" if windows else "devtools/bin/linux/protoc"
        path = self.source_path / relative
        if not path.is_file():
            raise ConanException(f"the SDK checkout has no protoc at {relative}")
        if not windows:
            path.chmod(0o755)  # Git may not preserve the executable bit.
        return path

    def build(self) -> None:
        protoc = self._protoc()
        for batch in self.PROTO_BATCHES:
            out_dir = self.build_path / "generated" / batch.out
            out_dir.mkdir(parents=True, exist_ok=True)
            subprocess.run([
                protoc,
                *(f"--proto_path={self.source_path / p}" for p in batch.paths),
                f"--cpp_out={out_dir}",
                *(self.source_path / f"{p}.proto" for p in batch.protos),
            ], check=True)

    def package(self) -> None:
        src, dst = self.source_path, self.package_path
        for tree in self.HEADER_TREES:
            for pattern in ("*.h", "*.hpp", "*.inl", "*.inc", "*.proto"):
                copy(self, pattern, src / tree, dst / tree)

        for pattern in ("*.h", "*.inc", "*.proto"):
            copy(self, pattern, src / PROTOBUF_SRC, dst / PROTOBUF_SRC)

        for source in self.ENGINE_SOURCES + self.PLUGIN_SOURCES:
            folder, file = os.path.split(source)
            copy(self, file, src / folder, dst / folder)

        copy(self, "*", self.build_path / "generated", dst / "generated")
        self._package_libs()
        copy(self, "cmake/hl2sdk-sources.cmake", self.recipe_folder, dst)
        copy(self, "LICENSE*", src, dst / "licenses")

    def _package_libs(self) -> None:
        src, dst = self.source_path, self.package_path
        if self.settings.os != "Linux":
            copy(self, "*", src / "lib/public/win64", dst / "lib/public/win64")
            return

        lib_dir = dst / "lib/linux64"
        copy(self, "*", src / "lib/linux64", lib_dir)

        # CMake needs lib-prefixed names; retain originals for path-based linking.
        for stem in ("mathlib", "interfaces"):
            plain = lib_dir / f"{stem}.a"
            if plain.is_file():
                shutil.copyfile(plain, lib_dir / f"lib{stem}.a")

    def package_info(self) -> None:
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
