import glob
import os

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class VoltModTestConan(ConanFile):
    """Build a hello plugin through voltmod_add_plugin to verify the consumer contract.

    This covers transitive SDK headers and libraries, per-plugin translation units,
    the precompiled header, and VDF generation.
    """

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
        ext = ".dll" if self.settings.os == "Windows" else ".so"
        modules = glob.glob(
            os.path.join(self.build_folder, "plugins", "hello-plugin", "*", f"hello-plugin{ext}"))
        assert modules, f"hello-plugin{ext} not produced under {self.build_folder}/plugins"
        vdf = os.path.join(self.build_folder, "hello-plugin.vdf")
        assert os.path.isfile(vdf), f"{vdf} not rendered"
