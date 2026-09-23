from rich.console import Console
from rich.text import Text

# Markup and highlighting stay off: messages hold paths, regexes and `[1/2]`, all taken literally.
# soft_wrap keeps a long path on one line when the output is piped or logged.
_out = Console(highlight=False, soft_wrap=True)
_err = Console(stderr=True, highlight=False, soft_wrap=True)


def step(text: str) -> None:
    """One stage of a longer command."""
    _out.print(f"==> {text}", style="bold cyan", markup=False)


def section(title: str) -> None:
    _out.print(f"--- {title} ---", style="bold", markup=False)


def item(text: str) -> None:
    """Something a step produced, such as a file it wrote."""
    _out.print(f"  -> {text}", markup=False)


def note(text: str) -> None:
    _out.print(f"  {text}", style="dim", markup=False)


def labelled(label: str, style: str, text: str) -> None:
    """A line led by a coloured label, such as a check's PASS or FAIL."""
    _out.print(Text.assemble((label, style), "  ", text))


def info(text: str = "") -> None:
    _out.print(text, markup=False)


def done(text: str) -> None:
    _out.print(text, style="bold green", markup=False)


def warn(text: str) -> None:
    _err.print(f"warning: {text}", style="yellow", markup=False)


def error(text: str) -> None:
    _err.print(f"error: {text}", style="bold red", markup=False)
