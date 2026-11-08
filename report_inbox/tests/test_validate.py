"""The inbox's manifest rules match the panel's."""

from __future__ import annotations

import copy
import unittest

from support import REPORT_ID, bundle

from bhp_report_inbox import validate


class ValidateTest(unittest.TestCase):
    def test_a_panel_manifest_passes(self) -> None:
        for kind in ("fault", "crash", "restart"):
            validate.validate_manifest(bundle(kind)[0], REPORT_ID)
        validate.validate_manifest(bundle("text", description="Skip did nothing.\nTwice.")[0], REPORT_ID)
        validate.validate_manifest(bundle("voice", audio=b"RIFF....")[0], REPORT_ID)

    def test_each_rule(self) -> None:
        base = bundle()[0]
        cases = [
            ("keys", lambda m: m.update(extra=1)), ("schema", lambda m: m.update(schema="v2")),
            ("report_id", lambda m: m.update(report_id="rpt-x")), ("kind", lambda m: m.update(kind="chat")),
            ("created_at", lambda m: m.update(created_at="yesterday")),
            ("component", lambda m: m.update(component="Music UI")),
            ("error_code", lambda m: m.update(error_code="lower.case")),
            ("location", lambda m: m.update(location="x" * 257)),
            ("build", lambda m: m["build"].update({"Bad Key": "x"})),
            ("screen", lambda m: m["screen"].update(surface="Music Screen")),
            ("trace_ids", lambda m: m.update(trace_ids=["has space"])),
            ("log_lines", lambda m: m.update(log_lines=True)),
            ("description_text", lambda m: m.update(description_text="automatic reports have none")),
            ("files.name", lambda m: m["files"].append({"name": "../x", "bytes": 1, "sha256": "0" * 64})),
            ("files.bytes", lambda m: m["files"][0].update(bytes=2 * 1024 * 1024)),
        ]
        for field, change in cases:
            manifest = copy.deepcopy(base)
            change(manifest)
            with self.assertRaises(validate.ValidationError, msg=field) as raised:
                validate.validate_manifest(manifest, REPORT_ID)
            self.assertEqual(raised.exception.field, field)
        with self.assertRaises(validate.ValidationError):
            validate.validate_manifest(bundle()[0], "rpt-" + "1" * 32)
        audio_on_text = bundle("text", audio=b"RIFF")[0]
        with self.assertRaises(validate.ValidationError):
            validate.validate_manifest(audio_on_text, REPORT_ID)
        control = bundle("text", description="tab\there")[0]
        with self.assertRaises(validate.ValidationError):
            validate.validate_manifest(control, REPORT_ID)

    def test_content_range(self) -> None:
        self.assertEqual(validate.parse_content_range("bytes 0-99/100"), (0, 99, 100))
        for bad in (None, "bytes 5-4/10", "bytes 0-10/10", "items 0-1/2", "bytes -1-2/3"):
            self.assertIsNone(validate.parse_content_range(bad))


if __name__ == "__main__":
    unittest.main()
