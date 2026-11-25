#!/usr/bin/env python3
"""Bench tool: show one GPIO line's live state, readable from across the room.

For FRDM-GATE-PIR bring-up on the NXP vendor image, so the person at the board
can wave at the PIR and see for themselves whether the line moves. It is not
evidence and writes no record; `frdm_pir_edge_capture.sh` stays the gate script.

Two views of the same state (level, rising-edge count, time since last change):
  default   a fullscreen window on the panel through the running Weston
            compositor (GTK 3 from the vendor image): bright with MOTION while
            the line is high, dark with "idle" while it is low.
  --text    one status line in the terminal, redrawn in place.

Inputs: --chip /dev/gpiochipN --line L (default /dev/gpiochip0 line 1, which is
  gpio1 line 1 = J18-12 on the FRDM-IMX8MPLUS), --active-low, --hz N (redraws
  and level samples per second, default 10), --seconds N (stop after N).
Needs: Python 3 standard library; the panel view also needs PyGObject with
  GTK 3 and a Wayland compositor (found through /run/user/<uid>/wayland-*).
Side effects: holds the line as an input with edge detection while it runs
  (GPIO character device, uAPI v2). It does not touch the pad mux, the
  backlight, or storage. The pad must already be muxed as a GPIO.
Exit: 0 normal end; 1 the line or the display could not be opened; 2 usage.
"""

from __future__ import annotations

import argparse
import fcntl
import glob
import os
import select
import signal
import struct
import sys
import time

# linux/gpio.h, uAPI v2.
_LINE_REQUEST_SIZE = 592
_LINE_EVENT_SIZE = 48
_GET_LINE_IOCTL = (3 << 30) | (_LINE_REQUEST_SIZE << 16) | (0xB4 << 8) | 0x07
_GET_VALUES_IOCTL = (3 << 30) | (16 << 16) | (0xB4 << 8) | 0x0E
_FLAG_ACTIVE_LOW = 1 << 1
_FLAG_INPUT = 1 << 2
_FLAG_EDGE_RISING = 1 << 4
_FLAG_EDGE_FALLING = 1 << 5
_EVENT_RISING = 1
_EVENT_FALLING = 2


class LineState:
    """Level, rising-edge count and time of the last change of one line.

    Fed from two sources: kernel edge events (exact, no pulse is missed) and
    periodic level samples (which also catch a change whose event was lost).
    Times are seconds on any monotonic clock the caller chooses.
    """

    def __init__(self, level: int, now: float) -> None:
        self.level = level
        self.rising_edges = 0
        self.last_change: float | None = None
        self.started = now

    def on_edge(self, rising: bool, now: float) -> None:
        if rising:
            self.rising_edges += 1
        self.level = 1 if rising else 0
        self.last_change = now

    def on_sample(self, level: int, now: float) -> None:
        if level == self.level:
            return
        if level == 1:
            self.rising_edges += 1
        self.level = level
        self.last_change = now

    def seconds_since_change(self, now: float) -> float | None:
        if self.last_change is None:
            return None
        return max(0.0, now - self.last_change)

    def summary(self, now: float) -> str:
        since = self.seconds_since_change(now)
        if since is None:
            changed = f"no change in {format_duration(now - self.started)}"
        else:
            changed = f"last change {format_duration(since)} ago"
        return f"rising edges: {self.rising_edges}   {changed}"


def format_duration(seconds: float) -> str:
    seconds = int(max(0.0, seconds))
    if seconds < 60:
        return f"{seconds} s"
    if seconds < 3600:
        return f"{seconds // 60} min {seconds % 60:02d} s"
    return f"{seconds // 3600} h {seconds % 3600 // 60:02d} min"


class GpioLine:
    """One input line with both-edge detection, through the GPIO chardev."""

    def __init__(self, chip_path: str, offset: int, active_low: bool) -> None:
        flags = _FLAG_INPUT | _FLAG_EDGE_RISING | _FLAG_EDGE_FALLING
        if active_low:
            flags |= _FLAG_ACTIVE_LOW
        request = bytearray(_LINE_REQUEST_SIZE)
        struct.pack_into("<I", request, 0, offset)  # offsets[0]
        consumer = b"bhp-pir-live-view"
        request[256 : 256 + len(consumer)] = consumer
        struct.pack_into("<Q", request, 288, flags)  # config.flags
        struct.pack_into("<I", request, 560, 1)  # num_lines
        chip = os.open(chip_path, os.O_RDONLY)
        try:
            fcntl.ioctl(chip, _GET_LINE_IOCTL, request)
        finally:
            os.close(chip)
        self.fd = struct.unpack_from("<i", request, 588)[0]
        os.set_blocking(self.fd, False)

    def level(self) -> int:
        values = bytearray(struct.pack("<QQ", 0, 1))
        fcntl.ioctl(self.fd, _GET_VALUES_IOCTL, values)
        return struct.unpack_from("<Q", values, 0)[0] & 1

    def read_edges(self) -> list[bool]:
        """Pending edges, oldest first; True is rising."""
        edges: list[bool] = []
        while True:
            try:
                data = os.read(self.fd, _LINE_EVENT_SIZE * 16)
            except BlockingIOError:
                return edges
            if not data:
                return edges
            for start in range(0, len(data) - _LINE_EVENT_SIZE + 1, _LINE_EVENT_SIZE):
                event_id = struct.unpack_from("<I", data, start + 8)[0]
                if event_id in (_EVENT_RISING, _EVENT_FALLING):
                    edges.append(event_id == _EVENT_RISING)

    def close(self) -> None:
        os.close(self.fd)


def poll_line(line: GpioLine, state: LineState) -> None:
    now = time.monotonic()
    for rising in line.read_edges():
        state.on_edge(rising, now)
    state.on_sample(line.level(), now)


def run_text(line: GpioLine, state: LineState, hz: int, seconds: float | None) -> int:
    deadline = None if seconds is None else time.monotonic() + seconds
    try:
        while deadline is None or time.monotonic() < deadline:
            select.select([line.fd], [], [], 1.0 / hz)
            poll_line(line, state)
            word = "MOTION" if state.level else "idle  "
            sys.stdout.write(f"\r{word}  level {state.level}   {state.summary(time.monotonic())}   ")
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    sys.stdout.write("\n")
    return 0


def find_wayland_environment() -> bool:
    """Points this process at a running compositor. False when there is none."""
    runtime = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    if os.environ.get("WAYLAND_DISPLAY") and os.environ.get("XDG_RUNTIME_DIR"):
        return True
    sockets = sorted(p for p in glob.glob(f"{runtime}/wayland-*") if not p.endswith(".lock"))
    if not sockets:
        return False
    os.environ["XDG_RUNTIME_DIR"] = runtime
    os.environ["WAYLAND_DISPLAY"] = os.path.basename(sockets[0])
    return True


def run_panel(line: GpioLine, state: LineState, hz: int, seconds: float | None) -> int:
    if not find_wayland_environment():
        print("FRDM_PIR_LIVE_VIEW.NO_COMPOSITOR: no wayland socket; use --text", file=sys.stderr)
        return 1
    os.environ.setdefault("GDK_BACKEND", "wayland")
    try:
        import gi

        gi.require_version("Gtk", "3.0")
        from gi.repository import GLib, Gtk
    except (ImportError, ValueError) as error:
        print(f"FRDM_PIR_LIVE_VIEW.NO_GTK: {error}; use --text", file=sys.stderr)
        return 1
    if not Gtk.init_check()[0]:
        print("FRDM_PIR_LIVE_VIEW.NO_DISPLAY: GTK could not open the display; use --text", file=sys.stderr)
        return 1

    window = Gtk.Window(title="PIR live view")
    area = Gtk.DrawingArea()
    window.add(area)

    def draw(_widget, context) -> bool:
        width = area.get_allocated_width()
        height = area.get_allocated_height()
        high = state.level == 1
        context.set_source_rgb(*((1.0, 0.85, 0.1) if high else (0.02, 0.02, 0.05)))
        context.paint()
        context.select_font_face("Sans", 0, 1)

        def centred(text: str, size: float, y: float) -> None:
            context.set_font_size(size)
            extents = context.text_extents(text)
            context.move_to((width - extents.width) / 2 - extents.x_bearing, y)
            context.show_text(text)

        context.set_source_rgb(*((0.0, 0.0, 0.0) if high else (0.45, 0.45, 0.55)))
        centred("MOTION" if high else "idle", height * 0.36, height * 0.55)
        context.set_source_rgb(*((0.0, 0.0, 0.0) if high else (0.8, 0.8, 0.85)))
        centred(state.summary(time.monotonic()), height * 0.065, height * 0.80)
        context.set_source_rgb(*((0.2, 0.2, 0.2) if high else (0.4, 0.4, 0.45)))
        centred(f"line level {state.level}   bench tool, not evidence", height * 0.04, height * 0.92)
        return False

    def tick() -> bool:
        poll_line(line, state)
        area.queue_draw()
        return True

    def on_edge_ready(_fd, _condition) -> bool:
        tick()
        return True

    area.connect("draw", draw)
    window.connect("destroy", Gtk.main_quit)
    GLib.timeout_add(max(1, 1000 // hz), tick)
    GLib.io_add_watch(line.fd, GLib.IO_IN, on_edge_ready)
    # Python runs these handlers when the tick below returns to the interpreter.
    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, lambda *_: Gtk.main_quit())
    if seconds is not None:
        GLib.timeout_add(int(seconds * 1000), Gtk.main_quit)
    window.fullscreen()
    window.show_all()
    Gtk.main()
    return 0


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--chip", default="/dev/gpiochip0")
    parser.add_argument("--line", type=int, default=1)
    parser.add_argument("--active-low", action="store_true")
    parser.add_argument("--hz", type=int, default=10)
    parser.add_argument("--seconds", type=float, default=None)
    parser.add_argument("--text", action="store_true", help="terminal view instead of the panel")
    options = parser.parse_args(arguments)
    if not 0 <= options.line <= 63 or not 1 <= options.hz <= 100:
        parser.error("--line is 0..63 and --hz is 1..100")
    try:
        line = GpioLine(options.chip, options.line, options.active_low)
    except OSError as error:
        print(
            f"FRDM_PIR_LIVE_VIEW.LINE_UNAVAILABLE: {options.chip} line {options.line}: {error}",
            file=sys.stderr,
        )
        return 1
    try:
        state = LineState(line.level(), time.monotonic())
        runner = run_text if options.text else run_panel
        return runner(line, state, options.hz, options.seconds)
    finally:
        line.close()


if __name__ == "__main__":
    sys.exit(main())
