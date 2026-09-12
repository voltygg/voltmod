import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from conan.tools.scm import Git


class Sqlpp23Conan(ConanFile):
    name = "sqlpp23"
    description = "Type-safe embedded domain-specific language for SQL queries in C++23"
    license = "BSD-2-Clause"
    homepage = "https://github.com/rbock/sqlpp23"
    package_type = "header-library"
    # The connector requirements resolve against the consumer's toolchain.
    settings = "os", "compiler", "build_type", "arch"
    no_copy_source = True

    options = {
        "with_postgresql": [True, False],
        "with_mariadb": [True, False],
        "with_sqlite3": [True, False],
    }
    default_options = {
        "with_postgresql": True,
        "with_mariadb": True,
        "with_sqlite3": True,
    }

    def set_version(self):
        pinned = list(self.conan_data["sources"])
        if len(pinned) != 1:
            raise ConanInvalidConfiguration("conandata.yml must pin exactly one version")
        self.version = pinned[0]

    def requirements(self):
        if self.options.with_postgresql:
            self.requires("libpq/[>=17 <18]", transitive_headers=True, transitive_libs=True)
        if self.options.with_mariadb:
            self.requires("mariadb-connector-c/[>=3.4 <4]",
                          transitive_headers=True, transitive_libs=True)
        if self.options.with_sqlite3:
            self.requires("sqlite3/[>=3.53 <4]", transitive_headers=True, transitive_libs=True)

    def package_id(self):
        self.info.clear()

    def source(self):
        pin = self.conan_data["sources"][self.version]
        Git(self).fetch_commit(url=pin["url"], commit=pin["commit"])

    def package(self):
        src, dst = self.source_folder, self.package_folder
        copy(self, "*", os.path.join(src, "include"), os.path.join(dst, "include"))
        copy(self, "sqlpp23-ddl2cpp", os.path.join(src, "scripts"), os.path.join(dst, "bin"))
        copy(self, "LICENSE*", src, os.path.join(dst, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "Sqlpp23")
        self.cpp_info.set_property("cmake_target_name", "sqlpp23::sqlpp23")

        core = self.cpp_info.components["core"]
        core.set_property("cmake_target_name", "sqlpp23::core")
        core.includedirs = ["include"]
        core.bindirs = []
        core.libdirs = []
        if self.settings.os == "Linux":
            core.system_libs = ["pthread"]

        if self.options.with_postgresql:
            postgresql = self.cpp_info.components["postgresql"]
            postgresql.set_property("cmake_target_name", "sqlpp23::postgresql")
            postgresql.requires = ["core", "libpq::pq"]

        if self.options.with_mariadb:
            mysql = self.cpp_info.components["mysql"]
            mysql.set_property("cmake_target_name", "sqlpp23::mysql")
            mysql.requires = ["core", "mariadb-connector-c::mariadb-connector-c"]
            if self.settings.os == "Windows":
                mysql.defines = ["NOMINMAX"]

        if self.options.with_sqlite3:
            sqlite3 = self.cpp_info.components["sqlite3"]
            sqlite3.set_property("cmake_target_name", "sqlpp23::sqlite3")
            sqlite3.requires = ["core", "sqlite3::sqlite3"]
