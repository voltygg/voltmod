"""The `voltmod database` commands: render migrations, and generate their table header."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod.database import DRIVERS, generate_table_header, render_migrations
from voltmod.files import write_or_check
from voltmod.project import Project

database_commands = typer.Typer(help="Migration rendering and table-header generation.")


@database_commands.command("tables")
def tables_command(
    migrations: Annotated[
        str, typer.Option("--migrations", help="Directory holding the NNNN_*.sql migrations")
    ],
    header: Annotated[str, typer.Option("--header", help="Table header to write")],
    namespace: Annotated[str, typer.Option("--namespace", help="C++ namespace for the tables")],
    check: Annotated[
        bool, typer.Option("--check", help="Fail instead of writing when out of date")
    ] = False,
) -> None:
    """Generate the sqlpp23 table header a plugin's migrations describe.

    The DDL parser reads the Postgres rendering and skips the indexes and seed rows it cannot
    parse, so the migrations stay the single source of truth.
    """
    project = Project.load()
    target = Path(header)
    ddl = render_migrations(Path(migrations), "postgres")
    header_text = generate_table_header(project.root, ddl, namespace, target.name)
    write_or_check(target, header_text, check=check)
    print(f"{target} is up to date." if check else f"Generated {target}.")


@database_commands.command("sql")
def sql_command(
    source: Annotated[
        str, typer.Argument(help="A .sql file, or a directory of NNNN_*.sql migrations")
    ],
    driver: Annotated[
        str, typer.Option("--driver", help=f"One of {', '.join(DRIVERS)}")
    ] = "postgres",
) -> None:
    """Print SQL as the server would apply it on a driver, ready to pipe into a client."""
    print(render_migrations(Path(source), driver))
