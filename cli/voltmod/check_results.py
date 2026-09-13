"""Results from doctor, source rules and screen checks, printed one way."""

from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass
from enum import StrEnum


class Status(StrEnum):
    PASS = "PASS"
    WARN = "WARN"
    FAIL = "FAIL"


@dataclass(frozen=True, slots=True)
class CheckResult:
    message: str
    status: Status = Status.FAIL
    hint: str = ""


def print_results(results: Iterable[CheckResult]) -> int:
    """Print results as they arrive, each hint once after its run of messages; return failures."""
    counts: Counter[Status] = Counter()
    hint = ""
    for result in results:
        if hint and result.hint != hint:
            print(f"      {hint}")
        hint = result.hint
        counts[result.status] += 1
        print(f"{result.status}  {result.message}")
    if hint:
        print(f"      {hint}")
    if counts:
        print(f"\n{counts[Status.FAIL]} failure(s), {counts[Status.WARN]} warning(s)")
    return counts[Status.FAIL]
