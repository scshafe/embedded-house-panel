"""Pinned Lexinet source fetch, against a local repository (no network)."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/lexinet"))

import pinned_source  # noqa: E402
from pinned_source import PinnedSourceError  # noqa: E402


def git(cwd: Path, *arguments: str) -> str:
    return subprocess.run(["git", *arguments], cwd=cwd, check=True, text=True,
                          capture_output=True).stdout.strip()


class PinnedSourceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-pinned-source-test-")
        base = Path(self.temporary.name)
        self.remote = base / "remote"
        self.remote.mkdir()
        git(self.remote, "init", "-q")
        (self.remote / "contracts").mkdir()
        (self.remote / "contracts/README.md").write_text("pinned\n", encoding="utf-8")
        git(self.remote, "add", ".")
        git(self.remote, "-c", "user.name=t", "-c", "user.email=t@example.invalid",
            "commit", "-q", "-m", "pinned")
        self.revision = git(self.remote, "rev-parse", "HEAD")
        (self.remote / "contracts/README.md").write_text("later\n", encoding="utf-8")
        git(self.remote, "-c", "user.name=t", "-c", "user.email=t@example.invalid",
            "commit", "-q", "-am", "later")
        self.url = self.remote.as_uri()
        self.cache = base / "cache"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def fetch(self, revision: str) -> Path:
        return pinned_source.fetch_pinned_source(revision, self.cache, self.url)

    def test_the_pin_is_the_checked_in_contract_revision(self) -> None:
        self.assertEqual(pinned_source.pinned_revision(),
                         "ce42272df315f357e10998dd48e9ffd1a935c2ff")

    def test_fetches_the_exact_pinned_commit_not_the_branch_head(self) -> None:
        checkout = self.fetch(self.revision)
        self.assertEqual(checkout, self.cache / self.revision)
        self.assertEqual(git(checkout, "rev-parse", "HEAD"), self.revision)
        self.assertEqual((checkout / "contracts/README.md").read_text(encoding="utf-8"),
                         "pinned\n")

    def test_reuses_a_clean_cache_without_the_network(self) -> None:
        checkout = self.fetch(self.revision)
        reused = pinned_source.fetch_pinned_source(self.revision, self.cache,
                                                   (self.remote / "missing").as_uri())
        self.assertEqual(reused, checkout)

    def test_a_changed_cache_fails_closed(self) -> None:
        checkout = self.fetch(self.revision)
        (checkout / "contracts/README.md").write_text("edited\n", encoding="utf-8")
        with self.assertRaises(PinnedSourceError) as raised:
            self.fetch(self.revision)
        self.assertEqual(raised.exception.code, "LEXINET_CACHE_INVALID")

    def test_a_cache_at_another_commit_fails_closed(self) -> None:
        checkout = self.fetch(self.revision)
        git(checkout, "fetch", "-q", self.url, "HEAD")
        git(checkout, "checkout", "-q", "--detach", "FETCH_HEAD")
        with self.assertRaises(PinnedSourceError) as raised:
            self.fetch(self.revision)
        self.assertEqual(raised.exception.code, "LEXINET_CACHE_INVALID")

    def test_an_unknown_commit_fails_and_leaves_no_cache(self) -> None:
        with self.assertRaises(PinnedSourceError) as raised:
            self.fetch("0" * 40)
        self.assertEqual(raised.exception.code, "LEXINET_PIN_FETCH_FAILED")
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_a_short_revision_is_rejected_before_any_fetch(self) -> None:
        with self.assertRaises(PinnedSourceError) as raised:
            self.fetch(self.revision[:12])
        self.assertEqual(raised.exception.code, "LEXINET_PIN_INVALID")
        self.assertFalse(self.cache.exists())

    def test_an_explicit_checkout_is_used_as_given(self) -> None:
        self.assertEqual(pinned_source.resolve_source(self.remote), (self.remote, "explicit"))


if __name__ == "__main__":
    unittest.main()
