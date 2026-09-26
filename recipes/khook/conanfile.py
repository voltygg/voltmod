# pyright: reportAttributeAccessIssue=false, reportOptionalSubscript=false

from typing import Any

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy
from conan.tools.scm import Git

KHOOK = "khook"
SAFETYHOOK = f"{KHOOK}/third_party/safetyhook"


class KHookConan(ConanFile):
    name = "khook"
    description = "KHook function and vtable hooking, with its safetyhook and Zydis backend"
    license = "Zlib"
    homepage = "https://github.com/Kenzzer/KHook"
    package_type = "static-library"
    settings: Any = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt"

    def set_version(self) -> None:
        pinned = list(self.conan_data["sources"])
        if len(pinned) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = pinned[0]

    def layout(self) -> None:
        cmake_layout(self)

    def source(self) -> None:
        pin = self.conan_data["sources"][self.version]
        # A subfolder, so KHook's CMakeLists.txt does not replace this recipe's.
        git = Git(self, folder=KHOOK)
        git.fetch_commit(url=pin["url"], commit=pin["commit"])
        # This safetyhook fork bundles Zydis as one file.
        git.run("submodule update --init --depth 1 third_party/safetyhook")

    def generate(self) -> None:
        toolchain = CMakeToolchain(self)
        toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        toolchain.generate()

    def build(self) -> None:
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self) -> None:
        src, dst = self.source_path, self.package_path
        copy(self, "*.hpp", src / KHOOK / "include", dst / "include")
        copy(self, "*.lib", self.build_path, dst / "lib", keep_path=False)
        copy(self, "*.a", self.build_path, dst / "lib", keep_path=False)
        copy(self, "LICENSE*", src / KHOOK, dst / "licenses")
        copy(self, "LICENSE*", src / SAFETYHOOK, dst / "licenses/safetyhook")

    def package_info(self) -> None:
        self.cpp_info.set_property("cmake_file_name", "khook")

        # Every hooking module compiles against this; its calls forward to the owning module.
        headers = self.cpp_info.components["headers"]
        headers.set_property("cmake_target_name", "khook::headers")
        headers.includedirs = ["include"]
        headers.libdirs = []
        headers.bindirs = []

        # The implementation, linked only by the module that owns it.
        implementation = self.cpp_info.components["khook"]
        implementation.set_property("cmake_target_name", "khook::khook")
        implementation.libs = ["khook"]
        implementation.defines = ["KHOOK_STANDALONE", "KHOOK_EXPORT"]
        implementation.requires = ["headers"]
        if self.settings.os == "Linux":
            implementation.system_libs = ["pthread"]
