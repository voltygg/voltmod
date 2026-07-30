import os

from conan import ConanFile
from conan.tools.files import copy
from conan.tools.scm import Git


class MetamodSourceConan(ConanFile):
    """Metamod:Source 2.0 headers (core + SourceHook) for CS2 plugin development.

    Header-only: nothing from Metamod is compiled; plugins implement ISmmPlugin
    and the loader lives on the server. The package mirrors the upstream layout
    (core/, core/sourcehook/) so include paths match the submodule consumption
    in cs2-kit's CS2KitSdk.cmake.
    """

    name = "metamod-source"
    description = "Metamod:Source 2.0 headers (core + SourceHook) for CS2 plugins"
    license = "Zlib"
    homepage = "https://github.com/alliedmodders/metamod-source"
    package_type = "header-library"
    no_copy_source = True

    def source(self):
        data = self.conan_data["sources"][self.version]
        Git(self).fetch_commit(url=data["url"], commit=data["commit"])

    def package(self):
        copy(self, "*.h", os.path.join(self.source_folder, "core"),
             os.path.join(self.package_folder, "core"))
        copy(self, "LICENSE*", self.source_folder,
             os.path.join(self.package_folder, "licenses"))

    def package_id(self):
        self.info.clear()

    def package_info(self):
        # Same target name CS2KitSdk.cmake gives the submodule-mode INTERFACE target.
        self.cpp_info.set_property("cmake_file_name", "metamod-source")
        self.cpp_info.set_property("cmake_target_name", "CS2Kit::Metamod")
        self.cpp_info.includedirs = ["core", "core/sourcehook"]
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
