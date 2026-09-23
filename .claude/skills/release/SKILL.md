---
name: release
description: Release a new voltmod version - bump conanfile.py, write the CHANGELOG entry, tag v<version> so the Release workflow uploads the Conan package, then relock consumers. Use for "release", "cut a release", "bump voltmod to X", "publish voltmod".
---

# Release voltmod

Pushing a `v<version>` tag on `main` runs `release.yml`: it rejects a tag that differs from
`conanfile.py`, uploads the Linux package to the `volty` remote and creates the GitHub release.

The user names the version. Consumers pin a range (cs2-plugins: `voltmod/[~1.5]`): a patch stays
inside it, a minor or major makes every consumer edit its range. If `!` commits landed since the
last tag, point them out before bumping only the patch.

## Steps

1. **Start clean**: on `main`, level with `origin/main`, `git tag -l v<version>` empty, and CI green
   on HEAD (`gh run list -R voltygg/voltmod --workflow ci.yml -b main -L 1`). Red: stop. Still
   running: do steps 2-4, then `gh run watch <id>` before step 5. This is the only CI wait.
2. **Collect changes**: `git log --format='%h %s%n%b' v<last>..HEAD`.
3. **CHANGELOG**: at the top, `## <version> (YYYY-MM-DD)` with `### Breaking`, `### New`,
   `### Fixed` (omit empty ones). Write for a plugin author deciding whether to upgrade:
   - one sentence per bullet, saying what they notice or must change; breaking bullets are
     instructions, not refactor reports
   - name a symbol only where they will type it; group renames into one bullet
   - no mechanics or motivation; link the docs when detail matters
   - six or seven bullets in total; skip ci, tests, style, internal refactors, and fixes for
     things that never shipped
4. **Bump** `version` in `conanfile.py` and `pyproject.toml`, run `uv lock`, and check
   `uv run poe release version` prints it.
5. **Commit and push** `conanfile.py`, `pyproject.toml`, `uv.lock`, `CHANGELOG.md` as
   `chore: release <version>`.
6. **Tag at once**: `git tag v<version> && git push origin v<version>`. The release commit only
   touches release files over the green commit, so its CI run is not awaited.
7. **Watch Release**: `gh run list -R voltygg/voltmod --workflow release.yml -L 1`, then
   `gh run watch <id>`. Both jobs must pass.
8. **Note the revision**: `uv run conan list "voltmod/<version>#*" -r volty`.
9. **Relock cs2-plugins** from its repo root, in the MSVC dev shell (`build-local`), with
   `voltmod/` at the tag:
   - `git -C voltmod ls-files --eol` must list no `w/crlf` or `w/mixed`: Conan hashes bytes on
     disk, so delete and `git checkout --` any it lists
   - `uv run conan editable add voltmod` unless `conan editable list` shows it
   - `uv run poe build --relock`; the revision in `conan.lock` must equal step 8's, or CI cannot
     resolve it
   - `uv lock --upgrade-package voltmod`
   - commit `conan.lock` and `uv.lock` as `chore: bump voltmod to <version>` with `commit`

## Never

- Tag a commit not on `origin/main`, or anything but the release commit on top of step 1's.
- Move, delete or re-push a pushed tag; fix a broken release with the next patch.
- Upload from a local machine.
- Commit a consumer `conan.lock` naming a revision the remote lacks.
