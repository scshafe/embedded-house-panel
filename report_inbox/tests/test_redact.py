"""Only allowlisted closed fields ever reach a public summary."""

from __future__ import annotations

import json
import unittest

from support import REPORT_ID, bundle

from bhp_report_inbox import redact

PRIVATE = ["Skip did nothing while Alex was cooking", "wifi-home-5g", "100.101.102.103", "t-1",
           "music_service.cpp:tick", "present", "day_active"]


class RedactTest(unittest.TestCase):
    def test_only_the_allowlist(self) -> None:
        manifest, _ = bundle()
        manifest["build"]["wifi_ssid"] = "wifi-home-5g"
        manifest["location"] = "music_service.cpp:tick"
        summary = redact.public_summary(manifest, {"reports": [REPORT_ID], "repeats": 4,
                                                   "first_seen": "2026-11-07T08:00:00Z",
                                                   "last_seen": "2026-11-08T12:00:00Z"})
        self.assertEqual(set(summary), {"report_id", "kind", "component", "error_code", "surface", "state",
                                        "fingerprint", "build", "count", "first_seen", "last_seen"})
        self.assertEqual(summary["count"], 5)
        self.assertEqual(summary["build"], {"board_id": "frdm-imx8mp", "project_commit": "abc123",
                                            "buildroot_version": "2026.05.1"})
        text = json.dumps(summary) + redact.issue_title(summary) + redact.issue_body(summary)
        for secret in PRIVATE:
            self.assertNotIn(secret, text)

    def test_a_manual_report_keeps_its_words_private(self) -> None:
        manifest, _ = bundle("text", description="Skip did nothing while Alex was cooking")
        summary = redact.public_summary(manifest, None)
        body = redact.issue_title(summary) + redact.issue_body(summary)
        self.assertIn("text report on music", body)
        for secret in PRIVATE:
            self.assertNotIn(secret, body)

    def test_a_bad_field_stops_the_summary(self) -> None:
        for field, value in (("error_code", "MUSIC.X` <img src=x>"), ("component", "music\nnote"),
                             ("report_id", "rpt-<script>")):
            manifest, _ = bundle()
            manifest[field] = value
            with self.assertRaises(redact.RedactionError):
                redact.public_summary(manifest, None)
        manifest, _ = bundle()
        manifest["build"]["project_commit"] = "abc 123; rm"
        with self.assertRaises(redact.RedactionError):
            redact.public_summary(manifest, None)


if __name__ == "__main__":
    unittest.main()
