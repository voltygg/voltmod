from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class Hl2SdkCs2TestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        # Compiling against tier0/mathlib headers with the package's define set
        # is the test; the full link is exercised by cs2-kit's test_package.
        pass
