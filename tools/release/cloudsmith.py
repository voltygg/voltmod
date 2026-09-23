import json
import urllib.error
import urllib.request
from typing import Any

from conan.tools.scm import Version

from tools.release.conan_packages import FRAMEWORK_PACKAGE, SDK_PACKAGES, references
from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.toolchain.conan import PACKAGE_REMOTE, conan_json

# Conan cannot delete revisions on Cloudsmith, so deletes go through its REST API.
API = "https://api.cloudsmith.io/v1/packages/volty/voltmod/"
PAGE_SIZE = 500


def prune(keep_versions: int, token: str, dry_run: bool) -> None:
    """Delete every artifact outside the newest revisions of the newest `keep_versions`."""
    reachable = _reachable_revisions(keep_versions)
    console.info(f"{len(reachable)} reachable revisions")

    artifacts = _published_artifacts(token)
    removed = 0
    for artifact in artifacts:
        revision = (artifact.get("identifiers") or {}).get("conan_revision_hash")
        if not revision or revision in reachable:
            continue
        name = f"{artifact['name']}/{artifact['version']}"
        console.item(f"remove {name} {artifact.get('filename')} #{revision[:12]}")
        removed += 1
        if not dry_run:
            _request("DELETE", f"{API}{artifact['slug_perm']}/", token)

    verb = "would remove" if dry_run else "removed"
    console.done(f"{verb} {removed} of {len(artifacts)} artifacts")


def _reachable_revisions(keep_versions: int) -> set[str]:
    reachable: set[str] = set()
    for name in (*SDK_PACKAGES, FRAMEWORK_PACKAGE):
        listing = conan_json("list", f"{name}/*#*:*#*", "-r", PACKAGE_REMOTE).get(
            PACKAGE_REMOTE, {}
        )
        versions: dict[str, dict[str, Any]] = {}
        for reference, body in references(listing, name).items():
            if "revisions" not in body:
                # Guessing here would delete everything a consumer resolves.
                raise VoltmodError(f"unexpected conan list output for {reference}: no revisions")
            versions.setdefault(reference.split("/", 1)[1], {}).update(body["revisions"])

        for version in sorted(versions, key=Version)[-keep_versions:]:
            recipe_revision = _newest(versions[version])
            reachable.add(recipe_revision)
            for package in versions[version][recipe_revision].get("packages", {}).values():
                if package_revisions := package.get("revisions"):
                    reachable.add(_newest(package_revisions))
    return reachable


def _published_artifacts(token: str) -> list[dict[str, Any]]:
    artifacts: list[dict[str, Any]] = []
    page = 1
    while True:
        try:
            batch = json.loads(_request("GET", f"{API}?page={page}&page_size={PAGE_SIZE}", token))
        except urllib.error.HTTPError as error:
            # Cloudsmith answers a page past the end with 404, not an empty list.
            if error.code == 404 and artifacts:
                return artifacts
            raise
        artifacts += batch
        if len(batch) < PAGE_SIZE:
            return artifacts
        page += 1


def _newest(revisions: dict[str, Any]) -> str:
    return max(revisions, key=lambda revision: revisions[revision].get("timestamp", 0))


def _request(method: str, url: str, token: str) -> bytes:
    headers = {"X-Api-Key": token} if token else {}
    request = urllib.request.Request(url, method=method, headers=headers)
    with urllib.request.urlopen(request) as response:
        return response.read()
