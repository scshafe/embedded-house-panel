#!/usr/bin/env python3
"""Board side of the FRDM panel UI demo (.claude/plans/2026-10-18-frdm-panel-ui-demo.md).

Runs as root on the NXP vendor image, from the unpacked bundle in /tmp/bhp-demo.
It writes only under /tmp/bhp-demo and /run/bhp-sim (both tmpfs), the backlight
and its own transient systemd units, whose output goes to /tmp/bhp-demo/logs
rather than the journal. It stops weston for the demo and starts it again in
`stop`. Every value it writes is a demo value; nothing here is gate evidence.

Usage: frdm_panel_demo_board.py start --rotation 0|180 | status |
       presence 0|1 | scenario NAME | screenshot NAME | stop
"""
from __future__ import annotations
import fcntl
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path('/tmp/bhp-demo')
RUN = Path('/run/bhp-sim')
STATE = ROOT/'state'
LOGS = ROOT/'logs'
EVIDENCE = 'demo_not_gate_evidence'
UI_UID = 910
VIDEO_GROUP = 'video'
UNITS = ('bhp-demo-ui', 'bhp-demo-supervisor', 'bhp-demo-pir')
WESTON_UNITS = ('weston.socket', 'weston.service')
TOUCH_NAME = 'Goodix Capacitive TouchScreen'
BACKLIGHT = Path('/sys/class/backlight/2-0045')
PIR_NAME = 'bhp-sim-pir'
# Paths this script may create or remove. tests/test_frdm_panel_demo.py checks them.
WRITABLE_ROOTS = (ROOT, RUN)


def invoke(*command: str, check: bool = True) -> str:
    return subprocess.run(command, check=check, capture_output=True, text=True).stdout.strip()


def require_root() -> None:
    if os.geteuid() != 0:
        raise ValueError('FRDM_DEMO.ROOT_REQUIRED')


def evdev_axis(device: Path, code: int) -> tuple[int, ...]:
    with device.open('rb', buffering=0) as handle:
        request = (2 << 30) | (24 << 16) | (ord('E') << 8) | (0x40 + code)
        return struct.unpack('iiiiii', fcntl.ioctl(handle, request, bytes(24)))


def input_device(name: str) -> Path:
    matches = [Path('/dev/input')/entry.name for entry in Path('/sys/class/input').glob('event*')
               if (entry/'device/name').read_text().strip() == name]
    if len(matches) != 1:
        raise ValueError('FRDM_DEMO.EXACT_INPUT_DEVICE_REQUIRED ' + name)
    return matches[0]


def touch_device() -> dict:
    device = input_device(TOUCH_NAME)
    x = evdev_axis(device, 0x35)  # ABS_MT_POSITION_X
    y = evdev_axis(device, 0x36)  # ABS_MT_POSITION_Y
    slots = evdev_axis(device, 0x2f)[2] + 1  # ABS_MT_SLOT maximum
    return {'path': str(device), 'name': TOUCH_NAME, 'protocol': 'type_b', 'x_min': x[1], 'x_max': x[2],
            'y_min': y[1], 'y_max': y[2], 'slots': slots, 'contact_key': 0}


def drm_device() -> dict:
    selected = []
    for connector in Path('/sys/class/drm').glob('card*-*'):
        if not (connector/'status').exists() or (connector/'status').read_text().strip() != 'connected':
            continue
        if '1024x600' not in (connector/'modes').read_text().splitlines():
            continue
        selected.append({'path': '/dev/dri/'+connector.name.split('-')[0],
                         'connector_id': int((connector/'connector_id').read_text())})
    if len(selected) != 1:
        raise ValueError('FRDM_DEMO.EXACT_DRM_CONNECTOR_REQUIRED')
    return selected[0]


def make_directory(path: Path, uid: int, gid: int, mode: int) -> None:
    path.mkdir(parents=True, exist_ok=True)
    os.chown(path, uid, gid)
    path.chmod(mode)


def configure(rotation: str) -> dict:
    """Writes the demo supervisor env and runtime record. Mirrors the VM guest's configure()."""
    make_directory(RUN, 0, 0, 0o755)
    for name in ('lifecycle', 'touch'):
        make_directory(RUN/name, 0, UI_UID, 0o2750)
    make_directory(RUN/'ui', UI_UID, UI_UID, 0o700)
    make_directory(STATE, 0, 0, 0o755)
    make_directory(STATE/'study', UI_UID, UI_UID, 0o700)
    make_directory(STATE/'screenshots', UI_UID, UI_UID, 0o755)
    make_directory(LOGS, 0, 0, 0o755)
    touch = touch_device()
    drm = drm_device()
    # Solar position is chosen so the supervisor computes day now, as in the VM guest.
    now = time.gmtime()
    utc_hour = now.tm_hour + now.tm_min/60 + now.tm_sec/3600
    longitude = ((12 - utc_hour)*15 + 180) % 360 - 180
    values = {'LATITUDE': 0, 'LONGITUDE': longitude, 'PRESENCE_GRACE_SECONDS': 3,
              'PRESENCE_INPUT_NAME': PIR_NAME, 'BACKLIGHT_PATH': str(BACKLIGHT/'brightness'),
              'BACKLIGHT_MAX_PATH': str(BACKLIGHT/'max_brightness'),
              'MODE_SOCKET': str(RUN/'lifecycle/mode.sock'), 'PANEL_UI_UID': UI_UID,
              'PRESENTATION_TIMEOUT_MILLISECONDS': 1500, 'DAY_BRIGHTNESS': 70, 'NIGHT_BRIGHTNESS': 12,
              'GRACE_DAY_BRIGHTNESS': 60, 'GRACE_NIGHT_BRIGHTNESS': 4, 'OFF_BRIGHTNESS': 0,
              'SUSPEND_MODE': 'disabled', 'POLL_INTERVAL_MILLISECONDS': 1000,
              'TOUCH_INPUT_PATH': touch['path'], 'TOUCH_INPUT_NAME': touch['name'],
              'TOUCH_PROTOCOL': touch['protocol'], 'TOUCH_X_MIN': touch['x_min'], 'TOUCH_X_MAX': touch['x_max'],
              'TOUCH_Y_MIN': touch['y_min'], 'TOUCH_Y_MAX': touch['y_max'], 'TOUCH_CONTACT_KEY': touch['contact_key'],
              'TOUCH_SLOTS': touch['slots'], 'TOUCH_SOCKET': str(RUN/'touch/observations.sock'),
              'TOUCH_TTL_MILLISECONDS': 1500, 'TOUCH_MAX_CONTACT_MILLISECONDS': 4000, 'TOUCH_MAX_OBSERVATIONS': 128}
    (STATE/'supervisor.env').write_text(''.join(f'BHP_PANEL_SUPERVISOR_{key}={value}\n' for key, value in values.items()))
    record = {'touch': touch, 'drm': drm, 'rotation': rotation, 'longitude': longitude, 'evidence': EVIDENCE}
    (STATE/'runtime.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def ui_command(record: dict) -> list[str]:
    touch, drm = record['touch'], record['drm']
    return [str(ROOT/'bin/bhp-panel-ui'), '--simulation-drm',
            '--socket-path', str(RUN/'lifecycle/mode.sock'), '--supervisor-uid', '0',
            '--drm-device', drm['path'], '--drm-connector-id', str(drm['connector_id']),
            '--touch-socket-path', str(RUN/'touch/observations.sock'),
            '--simulation-control-socket', str(RUN/'ui/control.sock'),
            '--scenario', str(ROOT/'scenarios/home.json'),
            '--launcher-config', str(ROOT/'config/applications.json'), '--clock-preview', 'on',
            '--state-directory', str(STATE/'study'), '--display-rotation', record['rotation'],
            '--touch-min-x', str(touch['x_min']), '--touch-max-x', str(touch['x_max']),
            '--touch-min-y', str(touch['y_min']), '--touch-max-y', str(touch['y_max'])]


def transient(unit: str, command: list[str], *properties: str) -> None:
    log = LOGS/(unit + '.log')
    invoke('systemd-run', '--unit=' + unit, '--collect', '--quiet',
           '-p', 'StandardOutput=append:' + str(log), '-p', 'StandardError=append:' + str(log),
           *[item for prop in properties for item in ('-p', prop)], *command)


def wait_for(check, code: str, seconds: float = 10) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.2)
    raise ValueError(code)


def unit_active(unit: str) -> bool:
    return invoke('systemctl', 'is-active', unit, check=False) == 'active'


def libraries_resolve() -> None:
    for binary in ('bhp-panel-ui', 'panel_supervisord', 'bhp-sim-pir'):
        result = subprocess.run(['ldd', str(ROOT/'bin'/binary)], capture_output=True, text=True)
        if result.returncode != 0 or 'not found' in result.stdout:
            raise ValueError('FRDM_DEMO.LIBRARY_MISSING ' + binary + ' ' + result.stdout.strip())


def start(rotation: str) -> dict:
    if rotation not in ('0', '180'):
        raise ValueError('FRDM_DEMO.ROTATION_INVALID')
    if any(unit_active(unit) for unit in UNITS):
        raise ValueError('FRDM_DEMO.ALREADY_RUNNING')
    libraries_resolve()
    record = configure(rotation)
    weston = [unit for unit in WESTON_UNITS if unit_active(unit)]
    (STATE/'weston-was-active.json').write_text(json.dumps(weston) + '\n')
    if weston:
        invoke('systemctl', 'stop', *weston)
    transient('bhp-demo-pir', [str(ROOT/'bin/bhp-sim-pir')])
    wait_for(lambda: any((entry/'device/name').read_text().strip() == PIR_NAME
                         for entry in Path('/sys/class/input').glob('event*')), 'FRDM_DEMO.PIR_DEVICE_MISSING')
    transient('bhp-demo-supervisor', [str(ROOT/'bin/panel_supervisord')],
              'EnvironmentFile=' + str(STATE/'supervisor.env'))
    wait_for(lambda: (RUN/'lifecycle/mode.sock').exists(), 'FRDM_DEMO.SUPERVISOR_SOCKET_MISSING')
    transient('bhp-demo-ui', ui_command(record), f'User={UI_UID}', f'Group={UI_UID}',
              'SupplementaryGroups=' + VIDEO_GROUP, 'NoNewPrivileges=yes', 'WorkingDirectory=' + str(ROOT))
    wait_for(lambda: (RUN/'ui/control.sock').exists(), 'FRDM_DEMO.UI_CONTROL_SOCKET_MISSING', 15)
    return status()


def latest_ui_status() -> dict | None:
    latest = None
    log = LOGS/'bhp-demo-ui.log'
    if log.exists():
        for line in log.read_text(errors='replace').splitlines()[-400:]:
            try:
                data = json.loads(line)['data']
            except (ValueError, KeyError, TypeError):
                continue
            scene = data.get('status', data) if isinstance(data, dict) else None
            if isinstance(scene, dict) and ('scenario_id' in scene or 'error_code' in scene):
                latest = scene
    return latest


def status() -> dict:
    return {'units': {unit: invoke('systemctl', 'is-active', unit, check=False) for unit in UNITS},
            'brightness': int((BACKLIGHT/'brightness').read_text()),
            'ui': latest_ui_status(), 'evidence': EVIDENCE}


def datagram(path: Path, payload: bytes) -> None:
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as channel:
        channel.sendto(payload, str(path))


def presence(value: str) -> dict:
    if value not in ('0', '1'):
        raise ValueError('FRDM_DEMO.PRESENCE_INVALID')
    datagram(RUN/'pir.sock', value.encode())
    time.sleep(1.2)
    return status()


def scenario(name: str) -> dict:
    if not re.fullmatch('[a-z0-9_-]{1,80}', name) or not (ROOT/'scenarios'/(name + '.json')).is_file():
        raise ValueError('FRDM_DEMO.SCENARIO_INVALID')
    datagram(RUN/'ui/control.sock', json.dumps({'type': 'scenario', 'path': str(ROOT/'scenarios'/(name + '.json'))}).encode())
    time.sleep(1.2)
    return status()


def screenshot(name: str) -> dict:
    if not re.fullmatch('[a-z0-9_-]{1,80}', name):
        raise ValueError('FRDM_DEMO.SCREENSHOT_NAME_INVALID')
    datagram(RUN/'ui/control.sock', json.dumps({'type': 'screenshot', 'path': str(STATE/'screenshots'/(name + '.png'))}).encode())
    time.sleep(1.2)
    return status()


def stop() -> dict:
    for unit in UNITS:
        invoke('systemctl', 'stop', unit, check=False)
        invoke('systemctl', 'reset-failed', unit, check=False)
    (BACKLIGHT/'brightness').write_text('255\n')
    weston_file = STATE/'weston-was-active.json'
    weston = json.loads(weston_file.read_text()) if weston_file.exists() else list(WESTON_UNITS)
    if weston:
        invoke('systemctl', 'start', *weston)
    shutil.rmtree(RUN, ignore_errors=True)
    shutil.rmtree(ROOT, ignore_errors=True)
    return {'units': {unit: invoke('systemctl', 'is-active', unit, check=False) for unit in UNITS},
            'weston': {unit: invoke('systemctl', 'is-active', unit, check=False) for unit in WESTON_UNITS},
            'removed': [str(path) for path in WRITABLE_ROOTS if not path.exists()], 'evidence': EVIDENCE}


def main(arguments: list[str]) -> dict:
    require_root()
    if arguments[:2] == ['start', '--rotation'] and len(arguments) == 3:
        return start(arguments[2])
    if arguments == ['status']:
        return status()
    if len(arguments) == 2 and arguments[0] == 'presence':
        return presence(arguments[1])
    if len(arguments) == 2 and arguments[0] == 'scenario':
        return scenario(arguments[1])
    if len(arguments) == 2 and arguments[0] == 'screenshot':
        return screenshot(arguments[1])
    if arguments == ['stop']:
        return stop()
    raise ValueError('FRDM_DEMO.USAGE')


if __name__ == '__main__':
    try:
        print(json.dumps({'data': main(sys.argv[1:]), 'error': None, 'evidence': EVIDENCE}))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(json.dumps({'data': None, 'error': {'code': 'FRDM_DEMO.FAILED', 'detail': str(error)}, 'evidence': EVIDENCE}))
        raise SystemExit(1)
