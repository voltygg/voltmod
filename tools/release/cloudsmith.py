"""Deleting published artifacts that no consumer can resolve any more."""

import json
import urllib.error
import urllib.request
from typing import Any

from tools.release.conan_packages import FRAMEWORK_PACKAGE, SDK_PACKAGES
from voltmod.conan import REMOTE, run_conan_json
from voltmod.errors import VoltmodError

# Conan cannot delete revisions on Cloudsmith, so deletes go through its REST API.
API = "https://api.cloudsmith.io/v1/packages/volty/voltmod/"
PAGE_SIZE = 500


def reachable_revisions(keep_versions: int) -> set[str]:
    """Every recipe and package revision a consumer can still resolve."""
    keep: set[str] = set()
    for name in (*SDK_PACKAGES, FRAMEWORK_PACKAGE):
        listing = run_conan_json("list", f"{name}/*#*:*#*", "-r", REMOTE).get(REMOTE, {})

        versions: dict[str, dict[str, Any]] = {}
        for reference, body in listing.items():
            revisions = body.get("revisions")
            if revisions is None:
                raise VoltmodError(f"unexpected conan list output for {reference}: no revisions")
            versions.setdefault(reference.split("/", 1)[1], {}).update(revisions)

        for version in sorted(versions)[-keep_versions:]:
            recipe_revision = _newest(versions[version])
            keep.add(recipe_revision)
            for package in versions[version][recipe_revision].get("packages", {}).values():
                if package_revisions := package.get("revisions", {}):
                    keep.add(_newest(package_revisions))
    return keep


def prune(keep_versions: int, token: str, dry_run: bool) -> None:
    reachable = reachable_revisions(keep_versions)
    print(f"{len(reachable)} reachable revisions")

    artifacts = _published_artifacts(token)
    removed = 0
    for artifact in artifacts:
        revision = (artifact.get("identifiers") or {}).get("conan_revision_hash")
        if not revision or revision in reachable:
            continue
        name = f"{artifact['name']}/{artifact['version']}"
        print(f"remove {name} {artifact.get('filename')} #{revision[:12]}")
        removed += 1
        if not dry_run:
            _request("DELETE", f"{API}{artifact['slug_perm']}/", token)

    verb = "would remove" if dry_run else "removed"
    print(f"{verb} {removed} of {len(artifacts)} artifacts")


def _published_artifacts(token: str) -> list[dict[str, Any]]:
    artifacts: list[dict[str, Any]] = []
    page = 1
    while True:
        try:
            batch = json.loads(_request("GET", f"{API}?page={page}&page_size={PAGE_SIZE}", token))
        except urllib.error.HTTPError as error:
            # Asking past the last page is a 404, not an empty page.
            if error.code == 404 and artifacts:
                return artifacts
            raise
        artifacts += batch
        if len(batch) < PAGE_SIZE:
            return artifacts
        page += 1


def _newest(entries: dict[str, Any]) -> str:
    return max(entries, key=lambda key: entries[key].get("timestamp", 0))


def _request(method: str, url: str, token: str) -> bytes:
    headers = {"X-Api-Key": token} if token else {}
    request = urllib.request.Request(url, method=method, headers=headers)
    with urllib.request.urlopen(request) as response:
        return response.read()
