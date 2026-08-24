"""Build and scaffolding tooling for voltmod and the projects that consume it.

Installed as a distribution (`voltmod`), so a consuming repo declares it as a
dependency instead of vendoring the framework for its scripts. Every entry point targets
`Path.cwd()`, so poe tasks run unchanged in either repo.
"""

from .buildtools import build, default_preset, ensure_remote, format_sources

__all__ = ["build", "default_preset", "ensure_remote", "format_sources"]
