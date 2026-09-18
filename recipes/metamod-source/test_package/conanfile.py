# pyright: reportOptionalCall=false

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class MetamodSourceTestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self) -> None:
        self.requires(self.tested_reference_str)

    def layout(self) -> None:
        cmake_layout(self)

    def build(self) -> None:
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self) -> None:
        pass
