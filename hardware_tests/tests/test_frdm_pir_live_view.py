#!/usr/bin/env python3
"""Tests for the PIR live view's state tracking and request layout (no GPIO, no display)."""

from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path
from unittest import mock

PROJECT_ROOT = Path(__file__).resolve().parents[2]
TOOL = PROJECT_ROOT / "hardware_tests/frdm/frdm_pir_live_view.py"
_spec = importlib.util.spec_from_file_location("frdm_pir_live_view", TOOL)
live_view = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = live_view
_spec.loader.exec_module(live_view)


class LineStateTest(unittest.TestCase):
    def test_counts_rising_edges_and_tracks_the_level(self) -> None:
        state = live_view.LineState(0, now=10.0)
        state.on_edge(True, 12.0)
        state.on_edge(False, 15.0)
        state.on_edge(True, 20.0)
        self.assertEqual((state.level, state.rising_edges), (1, 2))
        self.assertEqual(state.seconds_since_change(23.5), 3.5)

    def test_a_sample_that_agrees_with_the_edges_is_not_counted_twice(self) -> None:
        state = live_view.LineState(0, now=0.0)
        state.on_edge(True, 1.0)
        state.on_sample(1, 1.1)
        self.assertEqual(state.rising_edges, 1)
        self.assertEqual(state.last_change, 1.0)

    def test_a_sample_catches_a_change_with_no_event(self) -> None:
        state = live_view.LineState(0, now=0.0)
        state.on_sample(1, 2.0)
        state.on_sample(0, 3.0)
        self.assertEqual((state.level, state.rising_edges, state.last_change), (0, 1, 3.0))

    def test_summary_before_and_after_the_first_change(self) -> None:
        state = live_view.LineState(0, now=100.0)
        self.assertEqual(state.summary(165.0), "rising edges: 0   no change in 1 min 05 s")
        state.on_edge(True, 170.0)
        self.assertEqual(state.summary(174.9), "rising edges: 1   last change 4 s ago")

    def test_format_duration(self) -> None:
        self.assertEqual(live_view.format_duration(-1), "0 s")
        self.assertEqual(live_view.format_duration(59.9), "59 s")
        self.assertEqual(live_view.format_duration(3725), "1 h 02 min")


class GpioLineTest(unittest.TestCase):
    def test_request_asks_for_one_input_line_with_both_edges(self) -> None:
        seen = {}

        def fake_ioctl(fd, request, argument):
            seen["request"] = request
            seen["bytes"] = bytes(argument)
            struct.pack_into("<i", argument, 588, 42)
            return 0

        with mock.patch.object(live_view.os, "open", return_value=7), \
                mock.patch.object(live_view.os, "close"), \
                mock.patch.object(live_view.os, "set_blocking"), \
                mock.patch.object(live_view.fcntl, "ioctl", side_effect=fake_ioctl):
            line = live_view.GpioLine("/dev/gpiochip0", 1, active_low=False)

        self.assertEqual(line.fd, 42)
        # _IOWR(0xB4, 0x07, struct gpio_v2_line_request) from linux/gpio.h.
        self.assertEqual(seen["request"], 0xC250B407)
        raw = seen["bytes"]
        self.assertEqual(len(raw), 592)
        self.assertEqual(struct.unpack_from("<I", raw, 0)[0], 1)
        self.assertEqual(raw[256:288].rstrip(b"\0"), b"bhp-pir-live-view")
        self.assertEqual(struct.unpack_from("<Q", raw, 288)[0], 0x34)
        self.assertEqual(struct.unpack_from("<I", raw, 560)[0], 1)

    def test_read_edges_decodes_rising_and_falling_events(self) -> None:
        def event(event_id: int) -> bytes:
            return struct.pack("<QIIII24x", 123, event_id, 1, 0, 0)

        line = live_view.GpioLine.__new__(live_view.GpioLine)
        line.fd = 42
        with mock.patch.object(
            live_view.os, "read", side_effect=[event(1) + event(2) + event(1), BlockingIOError()]
        ):
            self.assertEqual(line.read_edges(), [True, False, True])


class MainTest(unittest.TestCase):
    def test_an_unopenable_line_is_a_structured_error(self) -> None:
        with mock.patch.object(live_view.sys, "stderr") as stderr:
            code = live_view.main(["--chip", "/nonexistent/gpiochip", "--text"])
        self.assertEqual(code, 1)
        self.assertIn("FRDM_PIR_LIVE_VIEW.LINE_UNAVAILABLE", stderr.write.call_args_list[0].args[0])


if __name__ == "__main__":
    unittest.main()
