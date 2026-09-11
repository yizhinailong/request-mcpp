"""Tag package.version changes between the endpoints of a GitHub push event."""

import os
from pathlib import Path
import re
import subprocess
import tomllib


def git(*args, check=True):
    return subprocess.run(
        ["git", *args], check=check, capture_output=True, text=True, encoding="utf-8"
    )


def version_at(commit):
    manifest = tomllib.loads(git("show", f"{commit}:mcpp.toml").stdout)
    version = manifest["package"]["version"]
    if not isinstance(version, str) or not version or version != version.strip():
        raise ValueError("[package].version must be a nonempty string without surrounding whitespace")
    return version


def tag_version(before, current):
    """Return the published tag, or an empty string when no version changed."""
    for commit in (before, current):
        if re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit) is None:
            raise ValueError("Push endpoints must be full Git commit hashes")

    if set(before) == {"0"}:
        print("No previous branch revision; establishing a baseline without a tag.")
        return ""

    # A force push can make the previous tip unreachable from the fetched history.
    if git("cat-file", "-e", f"{before}^{{commit}}", check=False).returncode != 0:
        git("fetch", "--no-tags", "origin", before)
    if git("cat-file", "-e", f"{before}:mcpp.toml", check=False).returncode != 0:
        print("No previous manifest; establishing a baseline without a tag.")
        return ""

    previous_version = version_at(before)
    current_version = version_at(current)
    if previous_version == current_version:
        print(f"Package version remains {current_version}; no tag created.")
        return ""

    tag = f"v{current_version}"
    ref = f"refs/tags/{tag}"
    git("check-ref-format", ref)
    existing = dict(
        (name, sha)
        for sha, name in (
            line.split() for line in git("ls-remote", "--tags", "origin", ref, f"{ref}^{{}}").stdout.splitlines()
        )
    )
    if ref in existing:
        target = existing.get(f"{ref}^{{}}", existing[ref])
        if target != current:
            raise ValueError(f"Tag {tag} already belongs to another commit; choose a new package version")
        print(f"Tag {tag} already points to this commit; Windows CI can be retried.")
        return tag

    # Push only this lightweight tag. Never move an existing tag or a branch.
    git("push", "origin", f"{current}:{ref}")
    print(f"Published {tag}: {previous_version} -> {current_version} at {current}")
    return tag


def main():
    tag = tag_version(os.environ["BEFORE_SHA"], os.environ["CURRENT_SHA"])
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        output.write(f"tag={tag}\n")


if __name__ == "__main__":
    main()

