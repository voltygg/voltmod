---
name: release
description: Release a new voltmod version - bump conanfile.py, write the CHANGELOG entry, tag v<version> so the Release workflow uploads the Conan package, then relock consumers. Use for "release", "cut a release", "bump voltmod to X", "publish voltmod".
---

# Release voltmod

A release is a `v<version>` tag on `main`. Pushing the tag runs
`.github/workflows/release.yml`: it refuses a tag that differs from `conanfile.py`
uploads the Linux Release package to the `volty` remote, then creates the GitHub release.

The user names the version. Consumers pin a range (cs2-plugins: `voltmod/[~1.4]`,
so 1.4.x only): a patch keeps it, a minor or major means every consumer edits its
range. Point out `!` commits since the last tag before bumping only the patch.

## Steps

1. **Check the start.** Clean tree on `main`, level with `origin/main`, the tag
   unused (`git tag -l v<version>`), and CI green on HEAD:
   `gh run list -R voltygg/voltmod --workflow ci.yml -b main -L 1`. Red or running: stop.
2. **Collect the changes:** `git log --format='%h %s%n%b' v<last>..HEAD`. The body
   line of a `!` commit says what consumers change.
3. **Write the CHANGELOG entry** at the top of `CHANGELOG.md` as
   `## <version> (YYYY-MM-DD)` with `### Breaking`, `### New` and `### Fixed` sections
   (omit empty ones). Release notes are for a plugin author deciding whether to upgrade,
   not for someone reading the diff:
   - One sentence per bullet, saying what they notice or what they must change. A breaking
     bullet reads as an instruction, not as a report of what was refactored.
   - Name a symbol only where they will type it. Group renames into one bullet instead of
     listing every pair, and leave out anything they never touch.
   - No mechanics: how the loader works, what a check rejects, why the change was made.
     Point at the docs when the detail matters.
   - Six or seven bullets across the whole entry is plenty. Skip ci, tests, style and
     internal refactors, and skip a fix for something that never shipped.
4. **Bump** `version` in `conanfile.py` and `pyproject.toml` to the same value, run
   `uv lock`, then confirm `uv run poe release version` prints it.
5. **Commit and push:** stage `conanfile.py`, `pyproject.toml`, `uv.lock` and
   `CHANGELOG.md` by name, `chore: release <version>`, `git push origin main`.
6. **Wait for CI** on that commit (`gh run watch <id> -R voltygg/voltmod`). Only a green
   commit gets tagged.
7. **Tag:** `git tag v<version> && git push origin v<version>`.
8. **Watch Release:** `gh run list -R voltygg/voltmod --workflow release.yml -L 1`,
   then `gh run watch`. Both jobs must pass.
9. **Confirm the remote** and note the revision:
   `uv run conan list "voltmod/<version>#*" -r volty`.
10. **Relock cs2-plugins** (MSVC dev shell, see its `/build-local` skill), with
    `vendor/voltmod` checked out at the tag:
    - in `vendor/voltmod`, `git ls-files --eol` must list no `w/crlf` or `w/mixed` file;
      Conan hashes the bytes on disk, so re-checkout any it lists (`rm <file>` then
      `git checkout -- <file>`)
    - `uv run conan editable add vendor/voltmod` if `conan editable list` is empty
    - `uv run poe build --relock`
    - the `voltmod/<version>#<revision>` in `conan.lock` must equal step 9's revision;
      a different one exists only locally and CI cannot resolve it
    - `uv lock --upgrade-package voltmod`
    - commit `conan.lock` and `uv.lock` as `chore: bump voltmod to <version>` via `/commit`

## Never

- Tag a commit that is not on `origin/main` or whose CI is not green.
- Move, delete or re-push a pushed tag. A broken release is fixed by the next patch.
- Upload from a local machine; the tag's workflow owns publishing.
- Commit a consumer `conan.lock` naming a revision the remote does not list.
