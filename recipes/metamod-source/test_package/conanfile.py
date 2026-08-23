from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class MetamodSourceTestConan(ConanFile):
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
        # Compilation is the test. (ISmmPlugin.h also needs hl2sdk headers;
        # cs2-kit's test_package covers that combination.)
        pass
