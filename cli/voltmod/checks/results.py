from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass
from enum import StrEnum
from itertools import groupby

import typer

from voltmod import console


class Status(StrEnum):
    PASS = "PASS"
    WARN = "WARN"
    FAIL = "FAIL"


_STYLES = {Status.PASS: "green", Status.WARN: "yellow", Status.FAIL: "bold red"}


@dataclass(frozen=True, slots=True)
class CheckResult:
    message: str
    status: Status
    hint: str = ""

    @classmethod
    def ok(cls, message: str) -> CheckResult:
        return cls(message, Status.PASS)

    @classmethod
    def warn(cls, message: str, hint: str = "") -> CheckResult:
        return cls(message, Status.WARN, hint)

    @classmethod
    def fail(cls, message: str, hint: str = "") -> CheckResult:
        return cls(message, Status.FAIL, hint)


def print_results(results: Iterable[CheckResult]) -> int:
    """Print results as they arrive, each hint once after its group; return the failure count."""
    counts: Counter[Status] = Counter()
    for hint, group in groupby(results, key=lambda result: result.hint):
        for result in group:
            counts[result.status] += 1
            console.labelled(result.status, _STYLES[result.status], result.message)
        if hint:
            console.note(f"    {hint}")
    if counts:
        console.info(f"\n{counts[Status.FAIL]} failure(s), {counts[Status.WARN]} warning(s)")
    return counts[Status.FAIL]


def exit_if_failed(results: Iterable[CheckResult]) -> None:
    if print_results(results):
        raise typer.Exit(1)
