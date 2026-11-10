#!/usr/bin/env python3
"""Board side of the bench tier (docs/decisions/0014-run-a-bench-tier-on-the-vendor-image.md).

Runs as root on NXP's vendor image, with the image's own python3. ADR 0014 limits what it writes:
- /opt/house-panel: releases, the `current` and `previous` links, install.json and the services' state/;
- the bench units in /etc/systemd/system, and weston's enablement and mask links;
- /run/house-panel and /run/bhp-sim (tmpfs);
- the backlight, through the supervisor it starts, and once more at uninstall.
It only reads /etc/house-panel, which the owner writes. It installs no package and adds no user:
services run under bare UIDs through setpriv. Every record says bench_not_gate_evidence.

Usage: bench_board.py install TARBALL SHA256 | rollback | uninstall [--purge] | status
       bench_board.py prepare | presence | exec SERVICE      (the units' entry points)
"""
from __future__ import annotations

import datetime
import fcntl
import grp
import hashlib
import json
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tarfile
import time
import zoneinfo
from pathlib import Path

EVIDENCE = 'bench_not_gate_evidence'
# The tests point PREFIX at a temporary directory; on the board it is /.
PREFIX = Path(os.environ.get('BHP_BENCH_PREFIX', '/'))
INSTALL = PREFIX/'opt/house-panel'
RELEASES = INSTALL/'releases'
CURRENT = INSTALL/'current'
PREVIOUS = INSTALL/'previous'
STATE = INSTALL/'state'
RECORD = INSTALL/'install.json'
CONFIG = PREFIX/'etc/house-panel'
UNIT_DIRECTORY = PREFIX/'etc/systemd/system'
RUN = PREFIX/'run/house-panel'
# bhp-sim-pir binds this exact path (scripts/sim/guest/pir_uinput.c).
SIM_RUN = PREFIX/'run/bhp-sim'
BOARD_INSTALL = Path('/opt/house-panel')  # what the units name, whatever PREFIX is

# ADR 0014, item 5: the production UIDs where they exist, bare (no user database entry).
UI_UID, BROKER_UID, MUSIC_UID, REPORTER_UID, UPLOADER_UID = 1102, 1103, 1105, 1106, 1107
# Hand-off groups: broker and uploader, broker and reporter; and every service's log tee.
VOICE_HANDOFF_GID, REPORT_HANDOFF_GID, INGEST_GID = 1108, 1109, 1110
SERVICE_UIDS = {'supervisor': 0, 'pir': 0, 'presence': 0, 'broker': BROKER_UID, 'reporter': REPORTER_UID,
                'music': MUSIC_UID, 'uploader': UPLOADER_UID, 'ui': UI_UID}
# Start order; optional services need their file in /etc/house-panel.
SERVICES = ('pir', 'presence', 'supervisor', 'reporter', 'broker', 'music', 'uploader', 'ui')
OPTIONAL = {'reporter': 'reporter.json', 'music': 'music.json', 'uploader': 'voice-lab.json'}
AFTER = {'presence': ('pir',), 'supervisor': ('pir',), 'broker': ('supervisor',),
         'ui': ('supervisor', 'broker', 'music', 'uploader', 'reporter')}
TARGET = 'bhp-bench.target'
WESTON_UNITS = ('weston.socket', 'weston.service')
BINARIES = ('bhp-panel-ui', 'panel_supervisord', 'bhp-sim-pir', 'bhp-media-broker', 'bhp-voice-lab-uploader',
            'bhp-music-control', 'bhp-reporter', 'bhp-log-tee')
BUNDLE_FILES = ('BUILD', 'SHA256SUMS', 'bench_board.py', 'config/applications.json', 'scenarios/home.json')
KEEP_RELEASES = 2

# The panel, as the demo found it on 2026-10-18 (docs/operations/frdm-bringup.md).
TOUCH_NAME = 'Goodix Capacitive TouchScreen'
BACKLIGHT = PREFIX/'sys/class/backlight/2-0045'
PIR_NAME = 'bhp-sim-pir'
# The array's ALSA device, measured on 2026-10-19 (docs/hardware/xvf3800-array-on-frdm.md).
ARRAY_PCM = 'hw:CARD=L16K6Ch,DEV=0'
PRESENCE_RESEND_SECONDS = 15
UTC = datetime.timezone.utc


class BenchError(Exception):
    """A refusal with a BENCH.* code; the CLI prints it as the error."""


# -- small helpers ------------------------------------------------------------------------

def systemctl(*arguments: str, check: bool = True) -> str:
    command = [os.environ.get('BHP_BENCH_SYSTEMCTL', 'systemctl'), *arguments]
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise BenchError('BENCH.SYSTEMCTL_FAILED ' + ' '.join(arguments))
    return result.stdout.strip()


def chown(path: Path, uid: int, gid: int) -> None:
    os.chown(path, uid, gid)


def make_directory(path: Path, uid: int, gid: int, mode: int) -> None:
    path.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or not path.is_dir():
        raise BenchError('BENCH.NOT_A_DIRECTORY ' + str(path))
    chown(path, uid, gid)
    path.chmod(mode)


def write_text(path: Path, text: str, mode: int = 0o644) -> None:
    temporary = path.with_name('.' + path.name + '.tmp')
    temporary.write_text(text)
    temporary.chmod(mode)
    os.replace(temporary, path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def link(path: Path, target: str) -> None:
    """Points `path` at `target` in one rename."""
    temporary = path.with_name('.' + path.name + '.new')
    if temporary.is_symlink() or temporary.exists():
        temporary.unlink()
    temporary.symlink_to(target)
    os.replace(temporary, path)


def release_of(path: Path) -> str | None:
    return os.readlink(path).rsplit('/', 1)[-1] if path.is_symlink() else None


def read_record() -> dict:
    try:
        return json.loads(RECORD.read_text())
    except (OSError, ValueError):
        return {}


# -- configuration (owner-written, outside Git) -----------------------------------------------

CLOCK = re.compile(r'([01][0-9]|2[0-3]):[0-5][0-9]')


def bench_config() -> dict:
    """/etc/house-panel/bench.json: location, time zone, rotation, awake hours and touch hold."""
    path = CONFIG/'bench.json'
    if not path.is_file():
        raise BenchError('BENCH.CONFIG_MISSING')
    try:
        config = json.loads(path.read_text())
    except ValueError:
        raise BenchError('BENCH.CONFIG_INVALID.JSON') from None
    allowed = {'latitude', 'longitude', 'timezone', 'rotation', 'awake', 'touch_presence_seconds'}
    if not isinstance(config, dict) or set(config) - allowed:
        raise BenchError('BENCH.CONFIG_INVALID.KEYS')

    def number(key: str, low: float, high: float) -> float:
        value = config.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
            raise BenchError('BENCH.CONFIG_INVALID.' + key.upper())
        return round(float(value), 4)

    latitude, longitude = number('latitude', -90, 90), number('longitude', -180, 180)
    timezone = config.get('timezone')
    try:
        zoneinfo.ZoneInfo(timezone if isinstance(timezone, str) and '..' not in timezone else '')
    except (zoneinfo.ZoneInfoNotFoundError, ValueError):
        raise BenchError('BENCH.CONFIG_INVALID.TIMEZONE') from None
    rotation = config.get('rotation', '180')
    if rotation not in ('0', '180'):
        raise BenchError('BENCH.CONFIG_INVALID.ROTATION')
    awake = config.get('awake', {'from': '06:30', 'to': '23:00'})
    if (not isinstance(awake, dict) or set(awake) != {'from', 'to'}
            or not all(isinstance(awake[k], str) and CLOCK.fullmatch(awake[k]) for k in awake)):
        raise BenchError('BENCH.CONFIG_INVALID.AWAKE')
    hold = config.get('touch_presence_seconds', 300)
    if isinstance(hold, bool) or not isinstance(hold, int) or not 1 <= hold <= 3600:
        raise BenchError('BENCH.CONFIG_INVALID.TOUCH_PRESENCE_SECONDS')
    return {'latitude': latitude, 'longitude': longitude, 'timezone': timezone, 'rotation': rotation,
            'awake': awake, 'touch_presence_seconds': hold}


def optional_config(service: str) -> dict | None:
    path = CONFIG/OPTIONAL[service]
    if not path.is_file():
        return None
    try:
        settings = json.loads(path.read_text())
    except ValueError:
        raise BenchError('BENCH.CONFIG_INVALID.' + service.upper()) from None
    if not isinstance(settings, dict):
        raise BenchError('BENCH.CONFIG_INVALID.' + service.upper())
    return settings


def https_url(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r'https://[a-z0-9.-]+(:[0-9]+)?', value) is not None


def reporter_settings() -> dict | None:
    """reporter.json: {"inbox_url": "https://<PC>:8743"} and optionally "description_channel" 0-5."""
    settings = optional_config('reporter')
    if settings is None:
        return None
    channel = settings.get('description_channel', 0)
    if (set(settings) - {'inbox_url', 'description_channel'} or not https_url(settings.get('inbox_url'))
            or isinstance(channel, bool) or not isinstance(channel, int) or not 0 <= channel <= 5):
        raise BenchError('BENCH.CONFIG_INVALID.REPORTER')
    return {'inbox_url': settings['inbox_url'], 'description_channel': channel}


def music_settings() -> dict | None:
    """music.json: the owner's Spotify app's client ID and the speaker's Connect name. No secret."""
    settings = optional_config('music')
    if settings is None:
        return None
    if (set(settings) != {'client_id', 'output_name'}
            or not re.fullmatch(r'[0-9a-f]{32}', str(settings.get('client_id')))
            or not re.fullmatch(r'[ -~]{1,64}', str(settings.get('output_name')))):
        raise BenchError('BENCH.CONFIG_INVALID.MUSIC')
    return settings


def voice_lab_settings() -> dict | None:
    """voice-lab.json, as the demo defines it: https issuer and API, the PC's node and tailnet."""
    settings = optional_config('uploader')
    if settings is None:
        return None
    if set(settings) - {'issuer', 'api_url', 'endpoint', 'tailnet_suffix', 'board_profile'}:
        raise BenchError('BENCH.CONFIG_INVALID.UPLOADER')
    if not (https_url(settings.get('issuer')) and https_url(settings.get('api_url'))):
        raise BenchError('BENCH.CONFIG_INVALID.UPLOADER')
    for key in ('endpoint', 'tailnet_suffix'):
        if not re.fullmatch(r'[a-z0-9.-]+', str(settings.get(key, ''))):
            raise BenchError('BENCH.CONFIG_INVALID.UPLOADER')
    return settings


def enabled_services() -> list[str]:
    return [name for name in SERVICES if name not in OPTIONAL or (CONFIG/OPTIONAL[name]).is_file()]


# -- the hardware the demo found -----------------------------------------------------------

def evdev_axis(device: Path, code: int) -> tuple[int, ...]:
    with device.open('rb', buffering=0) as handle:
        request = (2 << 30) | (24 << 16) | (ord('E') << 8) | (0x40 + code)
        return struct.unpack('iiiiii', fcntl.ioctl(handle, request, bytes(24)))


def input_device(name: str) -> Path:
    matches = [Path('/dev/input')/entry.name for entry in (PREFIX/'sys/class/input').glob('event*')
               if (entry/'device/name').read_text().strip() == name]
    if len(matches) != 1:
        raise BenchError('BENCH.EXACT_INPUT_DEVICE_REQUIRED')
    return matches[0]


def touch_device() -> dict:
    device = input_device(TOUCH_NAME)
    x, y = evdev_axis(device, 0x35), evdev_axis(device, 0x36)  # ABS_MT_POSITION_X, _Y
    slots = evdev_axis(device, 0x2f)[2] + 1  # ABS_MT_SLOT maximum
    return {'path': str(device), 'name': TOUCH_NAME, 'protocol': 'type_b', 'x_min': x[1], 'x_max': x[2],
            'y_min': y[1], 'y_max': y[2], 'slots': slots, 'contact_key': 0}


def drm_device() -> dict:
    selected = []
    for connector in (PREFIX/'sys/class/drm').glob('card*-*'):
        if not (connector/'status').exists() or (connector/'status').read_text().strip() != 'connected':
            continue
        if '1024x600' not in (connector/'modes').read_text().splitlines():
            continue
        selected.append({'path': '/dev/dri/' + connector.name.split('-')[0],
                         'connector_id': int((connector/'connector_id').read_text())})
    if len(selected) != 1:
        raise BenchError('BENCH.EXACT_DRM_CONNECTOR_REQUIRED')
    return selected[0]


def devices(seconds: float = 30) -> tuple[dict, dict]:
    """The touch device and the panel's connector; at boot they may appear after this unit starts."""
    deadline = time.monotonic() + seconds
    while True:
        try:
            return touch_device(), drm_device()
        except (BenchError, OSError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(1)


# -- prepare: runtime directories and the supervisor's settings, at every start --------------------

def supervisor_environment(config: dict, touch: dict) -> dict:
    return {'LATITUDE': config['latitude'], 'LONGITUDE': config['longitude'], 'PRESENCE_GRACE_SECONDS': 30,
            'PRESENCE_INPUT_NAME': PIR_NAME, 'BACKLIGHT_PATH': '/sys/class/backlight/2-0045/brightness',
            'BACKLIGHT_MAX_PATH': '/sys/class/backlight/2-0045/max_brightness',
            'MODE_SOCKET': '/run/house-panel/lifecycle/mode.sock', 'PANEL_UI_UID': UI_UID,
            'PRESENTATION_TIMEOUT_MILLISECONDS': 1500, 'DAY_BRIGHTNESS': 70, 'NIGHT_BRIGHTNESS': 12,
            'GRACE_DAY_BRIGHTNESS': 60, 'GRACE_NIGHT_BRIGHTNESS': 4, 'OFF_BRIGHTNESS': 0,
            'SUSPEND_MODE': 'disabled', 'POLL_INTERVAL_MILLISECONDS': 1000,
            'TOUCH_INPUT_PATH': touch['path'], 'TOUCH_INPUT_NAME': touch['name'],
            'TOUCH_PROTOCOL': touch['protocol'], 'TOUCH_X_MIN': touch['x_min'], 'TOUCH_X_MAX': touch['x_max'],
            'TOUCH_Y_MIN': touch['y_min'], 'TOUCH_Y_MAX': touch['y_max'], 'TOUCH_CONTACT_KEY': touch['contact_key'],
            'TOUCH_SLOTS': touch['slots'], 'TOUCH_SOCKET': '/run/house-panel/touch/observations.sock',
            'TOUCH_TTL_MILLISECONDS': 1500, 'TOUCH_MAX_CONTACT_MILLISECONDS': 4000, 'TOUCH_MAX_OBSERVATIONS': 128,
            'TOUCH_PRESENCE_SECONDS': config['touch_presence_seconds'],
            'MEDIA_BROKER_UID': BROKER_UID, 'MEDIA_WINDOW_SOCKET': '/run/house-panel/media/window.sock'}


def prepare() -> dict:
    config = bench_config()
    reporter, music, voice_lab = reporter_settings(), music_settings(), voice_lab_settings()
    make_directory(RUN, 0, 0, 0o755)
    make_directory(SIM_RUN, 0, 0, 0o755)
    for name in ('lifecycle', 'touch'):
        make_directory(RUN/name, 0, UI_UID, 0o2750)
    make_directory(RUN/'ui', UI_UID, UI_UID, 0o700)
    make_directory(RUN/'media', 0, BROKER_UID, 0o750)  # the window socket takes the broker's group
    make_directory(RUN/'config', 0, UI_UID, 0o750)
    # Each service's UI socket inherits the UI's group through the setgid bit.
    for name, uid in (('broker', BROKER_UID), ('music', MUSIC_UID), ('reporter', REPORTER_UID),
                      ('uploader', UPLOADER_UID)):
        make_directory(RUN/name, uid, UI_UID, 0o2750)
    # The reporter's ingest socket is 0660; the tees reach it through this directory's group.
    make_directory(RUN/'ingest', REPORTER_UID, INGEST_GID, 0o2750)
    make_directory(STATE, 0, 0, 0o755)
    make_directory(STATE/'study', UI_UID, UI_UID, 0o700)
    make_directory(STATE/'media', BROKER_UID, BROKER_UID, 0o700)
    make_directory(STATE/'voice-lab-handoff', UPLOADER_UID, VOICE_HANDOFF_GID, 0o2770)
    make_directory(STATE/'report-handoff', REPORTER_UID, REPORT_HANDOFF_GID, 0o2770)
    make_directory(STATE/'reporter', REPORTER_UID, REPORTER_UID, 0o700)
    make_directory(STATE/'music', MUSIC_UID, MUSIC_UID, 0o700)
    make_directory(STATE/'voice-lab', UPLOADER_UID, UPLOADER_UID, 0o700)
    study = CONFIG/'vocabulary-panel.env'
    if study.is_file():  # no credential (ADR 0010); readable by the UI's group only
        shutil.copyfile(study, RUN/'config/vocabulary-panel.env')
        chown(RUN/'config/vocabulary-panel.env', 0, UI_UID)
        (RUN/'config/vocabulary-panel.env').chmod(0o640)
    if voice_lab is not None and 'board_profile' in voice_lab:
        profile = STATE/'voice-lab/board-profile.json'
        write_text(profile, json.dumps(voice_lab['board_profile']) + '\n', 0o600)
        chown(profile, UPLOADER_UID, UPLOADER_UID)
    touch, drm = devices()
    values = supervisor_environment(config, touch)
    write_text(RUN/'supervisor.env', ''.join(f'BHP_PANEL_SUPERVISOR_{k}={v}\n' for k, v in values.items()), 0o600)
    record = {'touch': touch, 'drm': drm, 'rotation': config['rotation'], 'timezone': config['timezone'],
              'study_config': study.is_file(), 'reporter': reporter is not None, 'music': music is not None,
              'voice_lab': voice_lab is not None, 'evidence': EVIDENCE}
    write_text(RUN/'runtime.json', json.dumps(record, indent=2) + '\n')
    return record


# -- presence: the awake hours on the simulated PIR -------------------------------------------

def awake(now: datetime.datetime, hours: dict) -> bool:
    """True within [from, to) in local time; a range that passes midnight wraps."""
    minute = now.hour*60 + now.minute
    start, end = (int(hours[k][:2])*60 + int(hours[k][3:]) for k in ('from', 'to'))
    return start <= minute < end if start <= end else minute >= start or minute < end


def presence(cycles: int | None = None) -> None:
    """Holds the simulated PIR present during the awake hours; the supervisor adds touches."""
    config = bench_config()
    zone = zoneinfo.ZoneInfo(config['timezone'])
    done = 0
    while cycles is None or done < cycles:
        value = b'1' if awake(datetime.datetime.now(zone), config['awake']) else b'0'
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as channel:
                channel.sendto(value, str(SIM_RUN/'pir.sock'))
        except OSError:
            pass  # the PIR unit is restarting; the next pass sends again
        done += 1
        if cycles is None or done < cycles:
            time.sleep(PRESENCE_RESEND_SECONDS)


# -- exec: each unit's command line, under its own UID -------------------------------------------

def setpriv(uid: int, groups: list[int]) -> list[str]:
    if uid == 0:
        return []
    return ['setpriv', f'--reuid={uid}', f'--regid={uid}',
            f'--groups={",".join(map(str, groups))}' if groups else '--clear-groups',
            '--inh-caps=-all', '--bounding-set=-all', '--no-new-privs']


def group_id(name: str) -> int:
    return grp.getgrnam(name).gr_gid


def service_command(name: str, runtime: dict) -> list[str]:
    """The exact command line of one service, privileges dropped; read by `exec` and the tests."""
    bin_ = str(BOARD_INSTALL/'current/bin')
    run = '/run/house-panel'
    state = str(BOARD_INSTALL/'state')
    if name == 'pir':
        return [bin_ + '/bhp-sim-pir']
    if name == 'presence':
        return ['/usr/bin/python3', '-I', str(BOARD_INSTALL/'current/bench_board.py'), 'presence']
    if name == 'supervisor':
        return [bin_ + '/panel_supervisord']
    if name == 'broker':
        groups = [group_id('audio'), VOICE_HANDOFF_GID] + ([REPORT_HANDOFF_GID] if runtime.get('reporter') else [])
        return [*setpriv(BROKER_UID, groups), bin_ + '/bhp-media-broker', '--ui-socket', run + '/broker/ui.sock',
                '--ui-uid', str(UI_UID), '--window-socket', run + '/media/window.sock', '--supervisor-uid', '0',
                '--take-root', state + '/media/takes', '--pcm', ARRAY_PCM, '--maximum-seconds', '600',
                '--handoff-dir', state + '/voice-lab-handoff',
                *(['--report-handoff-dir', state + '/report-handoff'] if runtime.get('reporter') else [])]
    if name == 'reporter':
        settings = reporter_settings()
        if settings is None:
            raise BenchError('BENCH.SERVICE_NOT_CONFIGURED')
        ingest = ','.join(str(uid) for uid in sorted({0, UI_UID, BROKER_UID, MUSIC_UID, UPLOADER_UID}))
        return [*setpriv(REPORTER_UID, [REPORT_HANDOFF_GID]), bin_ + '/bhp-reporter',
                '--ui-socket', run + '/reporter/ui.sock', '--ui-uid', str(UI_UID),
                '--ingest-socket', run + '/ingest/ingest.sock', '--ingest-uids', ingest,
                '--outbox-dir', state + '/reporter/outbox', '--fault-dir', state + '/reporter/faults',
                '--inbox-url', settings['inbox_url'], '--build-info', str(BOARD_INSTALL/'current/BUILD'),
                '--handoff-dir', state + '/report-handoff',
                '--description-channel', str(settings['description_channel'])]
    if name == 'music':
        settings = music_settings()
        if settings is None:
            raise BenchError('BENCH.SERVICE_NOT_CONFIGURED')
        return [*setpriv(MUSIC_UID, []), bin_ + '/bhp-music-control', '--ui-socket', run + '/music/ui.sock',
                '--ui-uid', str(UI_UID), '--state-dir', state + '/music', '--client-id', settings['client_id'],
                '--output-name', settings['output_name']]
    if name == 'uploader':
        settings = voice_lab_settings()
        if settings is None:
            raise BenchError('BENCH.SERVICE_NOT_CONFIGURED')
        return [*setpriv(UPLOADER_UID, [VOICE_HANDOFF_GID]), bin_ + '/bhp-voice-lab-uploader',
                '--ui-socket', run + '/uploader/ui.sock', '--ui-uid', str(UI_UID),
                '--state-dir', state + '/voice-lab/state', '--handoff-dir', state + '/voice-lab-handoff',
                '--issuer', settings['issuer'], '--api-url', settings['api_url'], '--endpoint', settings['endpoint'],
                '--tailnet-suffix', settings['tailnet_suffix'],
                *(['--board-profile', state + '/voice-lab/board-profile.json'] if 'board_profile' in settings else [])]
    if name == 'ui':
        touch, drm = runtime['touch'], runtime['drm']
        return [*setpriv(UI_UID, [group_id('video')]), bin_ + '/bhp-panel-ui', '--simulation-drm',
                '--socket-path', run + '/lifecycle/mode.sock', '--supervisor-uid', '0',
                '--drm-device', drm['path'], '--drm-connector-id', str(drm['connector_id']),
                '--touch-socket-path', run + '/touch/observations.sock',
                '--simulation-control-socket', run + '/ui/control.sock',
                '--scenario', str(BOARD_INSTALL/'current/scenarios/home.json'),
                '--launcher-config', str(BOARD_INSTALL/'current/config/applications.json'), '--clock-preview', 'on',
                '--state-directory', state + '/study', '--display-rotation', runtime['rotation'],
                '--touch-min-x', str(touch['x_min']), '--touch-max-x', str(touch['x_max']),
                '--touch-min-y', str(touch['y_min']), '--touch-max-y', str(touch['y_max']),
                '--media-broker-socket', run + '/broker/ui.sock', '--media-broker-uid', str(BROKER_UID),
                *(['--study-config', run + '/config/vocabulary-panel.env'] if runtime.get('study_config') else []),
                *(['--voice-lab-uploader-socket', run + '/uploader/ui.sock',
                   '--voice-lab-uploader-uid', str(UPLOADER_UID)] if runtime.get('voice_lab') else []),
                *(['--music-control-socket', run + '/music/ui.sock',
                   '--music-control-uid', str(MUSIC_UID)] if runtime.get('music') else []),
                *(['--bug-reporter-socket', run + '/reporter/ui.sock',
                   '--bug-reporter-uid', str(REPORTER_UID)] if runtime.get('reporter') else [])]
    raise BenchError('BENCH.UNKNOWN_SERVICE')


def service_environment(name: str, runtime: dict) -> dict:
    environment = {'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'TZ': runtime['timezone'], 'LANG': 'C.UTF-8'}
    if name == 'supervisor':
        for line in (RUN/'supervisor.env').read_text().splitlines():
            key, _, value = line.partition('=')
            environment[key] = value
    return environment


def tee_command(name: str, *notice: str) -> list[str]:
    return [*setpriv(SERVICE_UIDS[name], [INGEST_GID]), str(BOARD_INSTALL/'current/bin/bhp-log-tee'),
            '--socket', '/run/house-panel/ingest/ingest.sock', '--service', 'bhp-' + name, *notice]


def exec_service(name: str) -> int:
    """Runs one service. With a reporter, its output also goes to it through bhp-log-tee, and an
    exit the unit will restart sends the restart notice; otherwise the service replaces this process."""
    if name not in SERVICES:
        raise BenchError('BENCH.UNKNOWN_SERVICE')
    runtime = json.loads((RUN/'runtime.json').read_text())
    command, environment = service_command(name, runtime), service_environment(name, runtime)
    if not runtime.get('reporter') or name in ('reporter', 'presence'):
        os.execve(command[0] if command[0].startswith('/') else shutil.which(command[0]) or command[0],
                  command, environment)
    stopping = {'requested': False}
    reader, writer = os.pipe()
    tee = subprocess.Popen(tee_command(name), stdin=reader, env=environment)
    os.close(reader)
    child = subprocess.Popen(command, stdout=writer, stderr=writer, env=environment)
    os.close(writer)

    def forward(signum: int, _frame: object) -> None:
        stopping['requested'] = True
        child.send_signal(signum)

    signal.signal(signal.SIGTERM, forward)
    signal.signal(signal.SIGINT, forward)
    status = child.wait()
    try:
        tee.wait(timeout=5)
    except subprocess.TimeoutExpired:
        tee.kill()
    if stopping['requested']:
        return 0
    notice = ['--exit-status', str(max(status, 0))] + (['--signal', str(-status)] if status < 0 else [])
    subprocess.run(tee_command(name, *notice, '--restarting'), env=environment, timeout=10, check=False)
    return status if status > 0 else 1


# -- units ---------------------------------------------------------------------------------

def unit_text(name: str) -> str:
    after = ' '.join(f'bhp-bench-{other}.service' for other in AFTER.get(name, ()))
    condition = f'ConditionPathExists=/etc/house-panel/{OPTIONAL[name]}\n' if name in OPTIONAL else ''
    return (f'# Installed by bench_board.py (ADR 0014); {EVIDENCE}. Removed by its uninstall.\n'
            f'[Unit]\nDescription=House panel bench: {name}\nPartOf={TARGET}\n'
            f'Requires=bhp-bench-prepare.service\nAfter=bhp-bench-prepare.service {after}'.rstrip() + '\n'
            f'{condition}StartLimitIntervalSec=0\n\n'
            f'[Service]\nExecStart=/usr/bin/python3 -I {BOARD_INSTALL}/current/bench_board.py exec {name}\n'
            'Restart=always\nRestartSec=2s\nRestartSteps=6\nRestartMaxDelaySec=2min\nKillMode=control-group\n')


def prepare_unit_text() -> str:
    return (f'# Installed by bench_board.py (ADR 0014); {EVIDENCE}. Removed by its uninstall.\n'
            f'[Unit]\nDescription=House panel bench: runtime directories and settings\nPartOf={TARGET}\n\n'
            '[Service]\nType=oneshot\nRemainAfterExit=yes\n'
            f'ExecStart=/usr/bin/python3 -I {BOARD_INSTALL}/current/bench_board.py prepare\n')


def target_text() -> str:
    units = ' '.join(['bhp-bench-prepare.service'] + [f'bhp-bench-{name}.service' for name in SERVICES])
    return (f'# Installed by bench_board.py (ADR 0014); {EVIDENCE}. Removed by its uninstall.\n'
            f'[Unit]\nDescription=House panel bench (ADR 0014)\nWants={units}\nAfter={units}\n\n'
            '[Install]\nWantedBy=multi-user.target\n')


def unit_files() -> dict[str, str]:
    files = {TARGET: target_text(), 'bhp-bench-prepare.service': prepare_unit_text()}
    files.update({f'bhp-bench-{name}.service': unit_text(name) for name in SERVICES})
    return files


# -- install, rollback, uninstall -------------------------------------------------------------

def safe_members(archive: tarfile.TarFile) -> list[tarfile.TarInfo]:
    members = []
    for member in archive.getmembers():
        name = member.name[2:] if member.name.startswith('./') else member.name
        if member.name in ('.', './'):
            continue
        if (not (member.isfile() or member.isdir()) or name.startswith('/') or '..' in name.split('/')
                or not re.fullmatch(r'[A-Za-z0-9._/-]+', name)):
            raise BenchError('BENCH.BUNDLE_MEMBER_REFUSED')
        members.append(member)
    return members


def check_bundle(directory: Path) -> dict:
    sums = {}
    for line in (directory/'SHA256SUMS').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9._/-]+)', line)
        if not match:
            raise BenchError('BENCH.SHA256SUMS_INVALID')
        sums[match[2]] = match[1]
    present = {str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file()} - {'SHA256SUMS'}
    if present != set(sums):
        raise BenchError('BENCH.BUNDLE_FILES_DIFFER')
    for name, digest in sums.items():
        if sha256_file(directory/name) != digest:
            raise BenchError('BENCH.BUNDLE_FILE_HASH_MISMATCH')
    for name in BUNDLE_FILES + tuple('bin/' + b for b in BINARIES):
        if not (directory/name).is_file():
            raise BenchError('BENCH.BUNDLE_FILE_MISSING')
    if sha256_file(directory/'bench_board.py') != sha256_file(Path(__file__)):
        raise BenchError('BENCH.INSTALLER_DIFFERS_FROM_BUNDLE')
    for binary in BINARIES:
        check_binary(directory/'bin'/binary)
    build = dict(line.split('=', 1) for line in (directory/'BUILD').read_text().splitlines() if '=' in line)
    return build


def check_binary(path: Path) -> None:
    """aarch64 ELF whose libraries the board resolves."""
    header = path.read_bytes()[:20]
    if header[:4] != b'\x7fELF' or int.from_bytes(header[18:20], 'little') != 0xB7:
        raise BenchError('BENCH.BINARY_NOT_AARCH64')
    path.chmod(0o755)
    result = subprocess.run(['ldd', str(path)], capture_output=True, text=True)
    if result.returncode != 0 or 'not found' in result.stdout:
        raise BenchError('BENCH.LIBRARY_MISSING')


def weston_state() -> dict:
    return {unit: systemctl('is-enabled', unit, check=False) or 'unknown' for unit in WESTON_UNITS}


def install_units() -> None:
    UNIT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    for name, text in unit_files().items():
        write_text(UNIT_DIRECTORY/name, text)
    systemctl('daemon-reload')


def install(tarball: str, expected: str) -> dict:
    if not re.fullmatch(r'[0-9a-f]{64}', expected):
        raise BenchError('BENCH.USAGE')
    # Without a valid configuration the bench cannot start, and weston would be off: refuse first.
    bench_config()
    reporter_settings(), music_settings(), voice_lab_settings()
    source = Path(tarball)
    if not source.is_file() or sha256_file(source) != expected:
        raise BenchError('BENCH.BUNDLE_HASH_MISMATCH')
    release = expected[:16]
    make_directory(INSTALL, 0, 0, 0o755)
    make_directory(RELEASES, 0, 0, 0o755)
    partial = RELEASES/(release + '.partial')
    shutil.rmtree(partial, ignore_errors=True)
    partial.mkdir()
    try:
        with tarfile.open(source, 'r:gz') as archive:
            archive.extractall(partial, members=safe_members(archive), filter='data')
        build = check_bundle(partial)
    except BaseException:
        shutil.rmtree(partial, ignore_errors=True)
        raise
    record = read_record()
    if 'weston_before' not in record:
        record['weston_before'] = weston_state()
    final = RELEASES/release
    if final.exists():
        shutil.rmtree(partial)
    else:
        os.replace(partial, final)
    old = release_of(CURRENT)
    if old and old != release:
        link(PREVIOUS, 'releases/' + old)
    link(CURRENT, 'releases/' + release)
    keep = {release_of(CURRENT), release_of(PREVIOUS)}
    for entry in RELEASES.iterdir():
        if entry.name not in keep:
            shutil.rmtree(entry)
    record.update({'current': release, 'previous': release_of(PREVIOUS), 'bundle_sha256': expected,
                   'build': build, 'installed_at': datetime.datetime.now(UTC).strftime('%Y-%m-%dT%H:%M:%SZ'),
                   'evidence': EVIDENCE})
    write_text(RECORD, json.dumps(record, indent=2) + '\n')
    install_units()
    for unit in WESTON_UNITS:
        systemctl('disable', '--now', unit, check=False)
        systemctl('mask', unit, check=False)
    systemctl('enable', TARGET)
    systemctl('restart', TARGET, check=False)
    return status()


def rollback() -> dict:
    current, previous = release_of(CURRENT), release_of(PREVIOUS)
    if not previous or not (RELEASES/previous).is_dir():
        raise BenchError('BENCH.NO_PREVIOUS_RELEASE')
    link(CURRENT, 'releases/' + previous)
    link(PREVIOUS, 'releases/' + current)
    record = read_record()
    record.update({'current': previous, 'previous': current,
                   'rolled_back_at': datetime.datetime.now(UTC).strftime('%Y-%m-%dT%H:%M:%SZ')})
    write_text(RECORD, json.dumps(record, indent=2) + '\n')
    install_units()  # the units name `current`; their text comes from the release now current
    systemctl('restart', TARGET, check=False)
    return status()


def uninstall(purge: bool) -> dict:
    record = read_record()
    systemctl('stop', TARGET, *[f'bhp-bench-{name}.service' for name in SERVICES], 'bhp-bench-prepare.service',
              check=False)
    systemctl('disable', TARGET, check=False)
    for name in unit_files():
        (UNIT_DIRECTORY/name).unlink(missing_ok=True)
    systemctl('daemon-reload', check=False)
    for unit in WESTON_UNITS:
        systemctl('unmask', unit, check=False)
    before = record.get('weston_before', {unit: 'enabled' for unit in WESTON_UNITS})
    enabled = [unit for unit in WESTON_UNITS if before.get(unit) == 'enabled']
    if enabled:
        systemctl('enable', *enabled, check=False)
    maximum = BACKLIGHT/'max_brightness'
    if maximum.exists():
        (BACKLIGHT/'brightness').write_text(maximum.read_text())
    if enabled:
        systemctl('start', *enabled, check=False)
    shutil.rmtree(RUN, ignore_errors=True)
    shutil.rmtree(SIM_RUN, ignore_errors=True)
    if purge:
        shutil.rmtree(INSTALL, ignore_errors=True)
    else:
        for path in (CURRENT, PREVIOUS):
            if path.is_symlink():
                path.unlink()
        shutil.rmtree(RELEASES, ignore_errors=True)
        RECORD.unlink(missing_ok=True)
    return {'removed': 'everything under /opt/house-panel' if purge else 'releases and units; state kept',
            'kept': ['/etc/house-panel'] + ([] if purge else ['/opt/house-panel/state']),
            'weston': {unit: systemctl('is-active', unit, check=False) for unit in WESTON_UNITS},
            'evidence': EVIDENCE}


def status() -> dict:
    units = ['bhp-bench-prepare.service'] + [f'bhp-bench-{name}.service' for name in SERVICES]
    try:
        config = {'bench': 'valid', **{k: v for k, v in bench_config().items() if k in ('timezone', 'rotation', 'awake')}}
    except BenchError as error:
        config = {'bench': str(error)}
    record = read_record()
    brightness = BACKLIGHT/'brightness'
    return {'current': release_of(CURRENT), 'previous': release_of(PREVIOUS), 'build': record.get('build'),
            'installed_at': record.get('installed_at'), 'config': config,
            'optional': {name: (CONFIG/file).is_file() for name, file in OPTIONAL.items()},
            'units': {unit: systemctl('is-active', unit, check=False) for unit in [TARGET, *units]},
            'brightness': int(brightness.read_text()) if brightness.exists() else None, 'evidence': EVIDENCE}


def main(arguments: list[str]) -> dict | None:
    if os.geteuid() != 0:
        raise BenchError('BENCH.ROOT_REQUIRED')
    if len(arguments) == 3 and arguments[0] == 'install':
        return install(arguments[1], arguments[2])
    if arguments == ['rollback']:
        return rollback()
    if arguments in (['uninstall'], ['uninstall', '--purge']):
        return uninstall(arguments[1:] == ['--purge'])
    if arguments == ['status']:
        return status()
    if arguments == ['prepare']:
        return prepare()
    if arguments == ['presence']:
        presence()
        return None
    if len(arguments) == 2 and arguments[0] == 'exec':
        raise SystemExit(exec_service(arguments[1]))
    raise BenchError('BENCH.USAGE')


if __name__ == '__main__':
    try:
        print(json.dumps({'data': main(sys.argv[1:]), 'error': None, 'evidence': EVIDENCE}), flush=True)
    except (BenchError, OSError, ValueError, KeyError, subprocess.SubprocessError, tarfile.TarError) as error:
        code = str(error) if isinstance(error, BenchError) else 'BENCH.FAILED ' + type(error).__name__
        print(json.dumps({'data': None, 'error': {'code': code}, 'evidence': EVIDENCE}), flush=True)
        raise SystemExit(1)
