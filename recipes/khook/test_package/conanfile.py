# pyright: reportOptionalCall=false

import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class KHookTestConan(ConanFile):
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
        if can_run(self):
            self.run(os.path.join(self.cpp.build.bindir, "khook-test"), env="conanrun")
