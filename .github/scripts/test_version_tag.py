"""Exercise real tag pushes against temporary local repositories, never GitHub."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("version_tag.py").resolve()


class VersionTagTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "work"
        self.remote = self.root / "remote.git"
        self.repo.mkdir()
        self.git("init", "--bare", str(self.remote))
        self.git("init", "-b", "main")
        self.git("config", "user.name", "CI test")
        self.git("config", "user.email", "ci@example.test")
        self.git("config", "commit.gpgsign", "false")
        self.git("remote", "add", "origin", str(self.remote))
        self.before = self.commit("0.1.0")
        self.git("push", "origin", "main")

    def git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.repo, check=True, capture_output=True, text=True
        ).stdout.strip()

    def commit(self, version, description="test"):
        (self.repo / "mcpp.toml").write_text(
            f'[package]\nname = "fixture"\nversion = "{version}"\ndescription = "{description}"\n'
            '[dependencies.compat]\ncurl = "8.21.0"\n',
            encoding="utf-8",
        )
        self.git("add", "mcpp.toml")
        self.git("commit", "-m", "Update manifest")
        return self.git("rev-parse", "HEAD")

    def run_tag(self, current, before=None, success=True):
        output = self.root / "github-output"
        output.write_text("", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, "-B", str(SCRIPT)],
            cwd=self.repo,
            env={**os.environ, "BEFORE_SHA": before or self.before, "CURRENT_SHA": current, "GITHUB_OUTPUT": str(output)},
            capture_output=True,
            text=True,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return output.read_text(encoding="utf-8").strip()

    def test_version_change_tags_exact_push_tip(self):
        self.commit("0.2.0")
        current = self.commit("0.2.0", "another commit in the same push")
        self.assertEqual(self.run_tag(current), "tag=v0.2.0")
        self.assertEqual(self.git("ls-remote", "origin", "refs/tags/v0.2.0").split()[0], current)
        self.assertEqual(self.git("ls-remote", "origin", "refs/heads/main").split()[0], self.before)

    def test_other_manifest_changes_do_not_tag(self):
        current = self.commit("0.1.0", "description changed")
        self.assertEqual(self.run_tag(current), "tag=")
        self.assertEqual(self.git("ls-remote", "--tags", "origin"), "")

    def test_version_changed_then_reverted_in_push_does_not_tag(self):
        self.commit("0.2.0")
        current = self.commit("0.1.0")
        self.assertEqual(self.run_tag(current), "tag=")
        self.assertEqual(self.git("ls-remote", "--tags", "origin"), "")

    def test_initial_push_does_not_tag_unchanged_baseline(self):
        self.assertEqual(self.run_tag(self.before, before="0" * 40), "tag=")
        self.assertEqual(self.git("ls-remote", "--tags", "origin"), "")

    def test_retry_reuses_tag_without_moving_it(self):
        current = self.commit("0.2.0")
        self.assertEqual(self.run_tag(current), "tag=v0.2.0")
        self.assertEqual(self.run_tag(current), "tag=v0.2.0")
        self.assertEqual(len(self.git("ls-remote", "--tags", "origin").splitlines()), 1)

    def test_existing_version_at_another_commit_is_rejected(self):
        self.git("push", "origin", f"{self.before}:refs/tags/v0.2.0")
        current = self.commit("0.2.0")
        self.assertEqual(self.run_tag(current, success=False), "")
        self.assertEqual(self.git("ls-remote", "origin", "refs/tags/v0.2.0").split()[0], self.before)

    def test_invalid_tag_name_is_rejected(self):
        current = self.commit("0.2.0 invalid")
        self.assertEqual(self.run_tag(current, success=False), "")
        self.assertEqual(self.git("ls-remote", "--tags", "origin"), "")

    def test_prerelease_version(self):
        current = self.commit("0.2.0-rc.1")
        self.assertEqual(self.run_tag(current), "tag=v0.2.0-rc.1")


if __name__ == "__main__":
    unittest.main()

