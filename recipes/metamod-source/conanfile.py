import os

import yaml
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git


class MetamodSourceConan(ConanFile):
    """Package Metamod:Source 2.0 headers for CS2 plugins.

    Plugins implement ISmmPlugin, and the server provides the loader. CMakeDeps supplies
    the header-only usage requirements, so no *-vars.cmake file is needed.
    `conandata.yml` pins the version and commit.
    """

    name = "metamod-source"
    description = "Metamod:Source 2.0 headers (core + SourceHook) for CS2 plugins"
    license = "Zlib"
    homepage = "https://github.com/alliedmodders/metamod-source"
    package_type = "header-library"
    no_copy_source = True

    def set_version(self):
        with open(os.path.join(self.recipe_folder, "conandata.yml"), encoding="utf-8") as handle:
            sources = yaml.safe_load(handle)["sources"]
        if len(sources) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = next(iter(sources))

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
        self.cpp_info.set_property("cmake_file_name", "metamod-source")
        self.cpp_info.set_property("cmake_target_name", "VoltMod::Metamod")
        self.cpp_info.includedirs = ["core", "core/sourcehook"]
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
