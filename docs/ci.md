# Version tags and Windows CI

The workflow structure follows `FileMonitor`: an independent version workflow
creates an annotated tag and explicitly dispatches a Windows workflow at that tag.
After the Windows build and tests succeed, a separate job publishes a GitHub Release.
All CI logic is inline in the two workflow YAML files; no separate scripts are
required. Inline Python uses the standard `tomllib` parser for package versions,
and PowerShell handles Git operations, workflow dispatch, and tool installation.

`.github/workflows/version-tag.yml` runs when a push to `main` changes `mcpp.toml`.
It parses `[package].version` as TOML at the push's previous and current commit,
so multi-commit pushes are compared as a whole. A version change such as
`0.1.0` to `0.2.0` creates the annotated tag `v0.2.0` at that push's final commit,
with the annotation `mcr 0.2.0` and the GitHub Actions bot as tagger.
Changes to dependencies, descriptions, formatting, or comments do not create
tags when the package version stays the same. Initial branch/manifest creation
establishes a baseline without a tag.

Existing tags are never overwritten. Reusing a version tagged at a different
commit fails the workflow; choose an unused version. Retrying the same push
reuses its existing tag (including earlier lightweight tags) and dispatches
Windows tests again. The workflows do not commit files or bump the package version.

The version workflow also supports manual runs on the default branch. A manual
run compares the selected commit with its first parent, so it still cannot tag
an unchanged version. For a multi-commit push, rerun the original workflow to
retain its original comparison endpoints. Concurrent attempts for the same
commit are serialized without grouping unrelated version changes together.

After publishing the tag, the version workflow runs
`gh workflow run windows-ci.yml --ref <tag>` using the built-in token. This is
necessary because pushes made with `GITHUB_TOKEN` do not start new push
workflows, as described in the
[GitHub triggering documentation](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).
The Windows workflow accepts `workflow_dispatch`, which GitHub permits the
built-in token to trigger, and also handles `v*` tags pushed by users. It can be
run manually from the Actions UI. Ordinary branch pushes and pull requests do
not start Windows tests. Duplicate Windows runs for the same ref cancel older
in-progress runs, as in FileMonitor.

Both workflows use `windows-2025`. The Windows job checks out the selected ref,
checks that a tag matches `[package].version`, prepares the MSVC x64 environment,
and installs the checksum-pinned official
`mcpp` Windows release `2026.9.11.1` into the runner's temporary directory,
installs and selects `llvm@22.1.8`, and runs `mcpp build` followed by `mcpp test`.
The hosted Windows image supplies the Visual Studio/Windows SDK installation required by
the LLVM MSVC target. Update the version and checksum together in
the `Install mcpp` step in `windows-ci.yml` when upgrading mcpp.

The tag job requests `contents: write` and `actions: write`, and the release job
requests `contents: write`; Windows tests use read-only repository access.
The built-in token is sufficient, with no additional secret required.
Repository rules must allow the tag job to create version tags. A failed
Windows test leaves the tag in place and marks the independent Windows CI run
as failed; rerun that workflow after investigating. A successful version run
means the tag exists and Windows CI was dispatched, not that tests have passed.

The `release` job runs only for `v*` tag refs after the `test` job succeeds.
It uses `gh release create --verify-tag --generate-notes` to publish a release
titled `mcr <version>`. The tag must already exist; publishing a release cannot
create an additional tag. Prerelease versions such as `0.2.0-rc.1` are marked
as prereleases. An existing release is left intact when the workflow is retried.
Branch-based manual runs and failed builds or tests do not publish releases.
GitHub provides the source archives associated with the release tag; this module
library does not add executable build artifacts. See the
[GitHub CLI release documentation](https://cli.github.com/manual/gh_release_create)
for the release creation flags.

Local validation:

```sh
actionlint .github/workflows/version-tag.yml .github/workflows/windows-ci.yml
mcpp test
```
