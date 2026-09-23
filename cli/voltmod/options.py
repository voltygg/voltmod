from typing import Annotated

import typer

ServerPath = Annotated[
    str, typer.Option("--server-path", help="CS2 server root (default: CS2_SERVER_PATH)")
]
PresetArgument = Annotated[
    str | None,
    typer.Argument(help="CMake preset (default: CS2_BUILD_PRESET, else release for this OS)"),
]
