# pyright: reportOptionalSubscript=false

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git

KHOOK = "third_party/khook"


class MetamodSourceConan(ConanFile):
    name = "metamod-source"
    description = "Metamod:Source 2.0 headers (core + KHook) for CS2 plugins"
    license = "Zlib"
    homepage = "https://github.com/alliedmodders/metamod-source"
    package_type = "header-library"
    no_copy_source = True

    def set_version(self) -> None:
        pinned = list(self.conan_data["sources"])
        if len(pinned) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = pinned[0]

    def source(self) -> None:
        pin = self.conan_data["sources"][self.version]
        git = Git(self)
        git.fetch_commit(url=pin["url"], commit=pin["commit"])
        git.run(f"submodule update --init --depth 1 {KHOOK}")

    def package(self) -> None:
        src, dst = self.source_path, self.package_path
        copy(self, "*.h", src / "core", dst / "core")
        copy(self, "*.hpp", src / KHOOK / "include", dst / KHOOK / "include")
        copy(self, "LICENSE*", src, dst / "licenses")
        copy(self, "LICENSE*", src / KHOOK, dst / "licenses/khook")

    def package_info(self) -> None:
        self.cpp_info.set_property("cmake_file_name", "metamod-source")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::Metamod")
        self.cpp_info.includedirs = ["core", f"{KHOOK}/include"]
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
