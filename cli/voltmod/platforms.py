import sys
from enum import StrEnum


class Platform(StrEnum):
    WINDOWS = "windows"
    LINUX = "linux"

    @classmethod
    def host(cls) -> Platform:
        return cls.WINDOWS if sys.platform == "win32" else cls.LINUX

    @property
    def bin_dir(self) -> str:
        """The game's binary folder name, as in `game/bin/<bin_dir>/`."""
        return "win64" if self is Platform.WINDOWS else "linuxsteamrt64"

    @property
    def library_suffix(self) -> str:
        return ".dll" if self is Platform.WINDOWS else ".so"

    @property
    def server_executable(self) -> str:
        """The dedicated server, relative to the install root."""
        name = "cs2.exe" if self is Platform.WINDOWS else "cs2"
        return f"game/bin/{self.bin_dir}/{name}"
