from pathlib import Path
from typing import Annotated

import typer

from voltmod.database.header import generate_table_header
from voltmod.database.migrations import DRIVERS, render_migrations
from voltmod.errors import VoltmodError
from voltmod.files import write_or_check
from voltmod.options import PluginNames, current_project


def header_command(
    plugins: PluginNames = None,
    check: Annotated[
        bool, typer.Option("--check", help="Fail instead of writing when out of date")
    ] = False,
) -> None:
    """Generate the sqlpp23 table header each plugin's migrations describe.

    The paths and namespace come from the `database` block of plugin.json. The DDL parser
    reads the Postgres rendering and skips the indexes and seed rows it cannot parse, so the
    migrations stay the single source of truth.
    """
    project = current_project()
    if plugins:
        selected = [project.plugin(name) for name in plugins]
    else:
        selected = [plugin for plugin in project.plugins() if plugin.database]
        if not selected:
            raise VoltmodError("no plugin.json under plugins/ has a database block")

    for plugin in selected:
        database = plugin.database
        if database is None:
            raise VoltmodError(f"{plugin.manifest_path} has no database block")
        ddl = render_migrations(database.migrations, "postgres")
        text = generate_table_header(project.root, ddl, database.namespace, database.header.name)
        write_or_check(database.header, text, check=check)
        header = database.header.relative_to(project.root)
        print(f"{header} is up to date." if check else f"Generated {header}.")


def sql_command(
    source: Annotated[
        Path, typer.Argument(help="A .sql file, or a directory of NNNN_*.sql migrations")
    ],
    driver: Annotated[
        str, typer.Option("--driver", help=f"One of {', '.join(DRIVERS)}")
    ] = "postgres",
) -> None:
    """Print SQL as the server would apply it on a driver, ready to pipe into a client."""
    print(render_migrations(source, driver))
