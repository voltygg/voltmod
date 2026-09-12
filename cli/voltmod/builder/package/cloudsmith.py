"""Delete artifacts no consumer can resolve.

Cloudsmith cannot delete revisions through Conan, so this works over its REST API and asks Conan
only which revisions are still reachable.
"""

import json
import urllib.request

from ... import tools
from . import conan

OWNER, REPOSITORY = "volty", "voltmod"
API = f"https://api.cloudsmith.io/v1/packages/{OWNER}/{REPOSITORY}/"


def _newest(entries: dict) -> str:
    return max(entries, key=lambda key: entries[key].get("timestamp", 0))


def reachable_revisions(keep_versions: int) -> set[str]:
    """Every recipe and package revision a consumer can still resolve."""
    keep: set[str] = set()
    for name in (*conan.SDK_PACKAGES, conan.FRAMEWORK_PACKAGE):
        query = (f"{name}/*#*:*#*", "-r", tools.CONAN_REMOTE, "--format=json")
        listing = json.loads(conan.run("list", *query, capture=True)).get(tools.CONAN_REMOTE, {})

        versions: dict[str, dict] = {}
        for ref, body in listing.items():
            revisions = body.get("revisions")
            if revisions is None:
                tools.abort(f"unexpected conan list output for {ref}: no 'revisions' key")
            versions.setdefault(ref.split("/", 1)[1], {}).update(revisions)

        for version in sorted(versions)[-keep_versions:]:
            recipe_rev = _newest(versions[version])
            keep.add(recipe_rev)
            for body in versions[version][recipe_rev].get("packages", {}).values():
                if package_revs := body.get("revisions", {}):
                    keep.add(_newest(package_revs))
    return keep


def _request(method: str, url: str, token: str) -> bytes:
    request = urllib.request.Request(url, method=method, headers={"X-Api-Key": token})
    with urllib.request.urlopen(request) as response:
        return response.read()


def prune(keep: int, token: str, dry_run: bool) -> None:
    reachable = reachable_revisions(keep)
    print(f"{len(reachable)} reachable revisions")

    url = API + "?page_size=500"
    raw = _request("GET", url, token) if token else urllib.request.urlopen(url).read()
    rows = json.loads(raw)

    removed = 0
    for row in rows:
        revision = (row.get("identifiers") or {}).get("conan_revision_hash")
        if not revision or revision in reachable:
            continue
        print(f"remove {row['name']}/{row['version']} {row.get('filename')} #{revision[:12]}")
        removed += 1
        if not dry_run:
            _request("DELETE", f"{API}{row['slug_perm']}/", token)

    verb = "would remove" if dry_run else "removed"
    print(f"{verb} {removed} of {len(rows)} artifacts")
