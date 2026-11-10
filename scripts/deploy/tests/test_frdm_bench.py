"""The bench tier's scripts (ADR 0014) against a fake board root: install, rollback, uninstall, the
owner's configuration, each service's command line, the units and the presence schedule."""

from __future__ import annotations

import datetime
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

DEPLOY = Path(__file__).resolve().parents[1]
BOARD_SCRIPT = DEPLOY/'frdm_bench/bench_board.py'
PC_SCRIPT = DEPLOY/'frdm_bench/bench_pc.py'
AARCH64_ELF = b'\x7fELF\x02\x01\x01' + bytes(11) + (0xB7).to_bytes(2, 'little') + bytes(44)
CONFIG = {'latitude': 40.71, 'longitude': -74.01, 'timezone': 'America/New_York', 'rotation': '180',
          'awake': {'from': '06:30', 'to': '23:00'}, 'touch_presence_seconds': 300}
TOUCH = {'path': '/dev/input/event5', 'name': 'Goodix Capacitive TouchScreen', 'protocol': 'type_b', 'x_min': 0,
         'x_max': 799, 'y_min': 0, 'y_max': 479, 'slots': 10, 'contact_key': 0}
DRM = {'path': '/dev/dri/card1', 'connector_id': 37}


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BoardCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.prefix = Path(self.temporary.name)/'board'
        self.prefix.mkdir()
        self.calls = Path(self.temporary.name)/'systemctl.log'
        fake = Path(self.temporary.name)/'systemctl'
        fake.write_text(f'#!/bin/sh\necho "$*" >> {self.calls}\n'
                        'case "$1" in is-enabled) echo enabled ;; is-active) echo inactive ;; esac\n')
        fake.chmod(0o755)
        environment = mock.patch.dict(os.environ, {'BHP_BENCH_PREFIX': str(self.prefix),
                                                   'BHP_BENCH_SYSTEMCTL': str(fake)})
        environment.start()
        self.addCleanup(environment.stop)
        self.board = load('bench_board_under_test', BOARD_SCRIPT)
        # Bundles name the installer they carry; the one under test runs from here.
        self.board.__file__ = str(BOARD_SCRIPT)
        for name, value in (('chown', lambda *args: None), ('check_binary', self.fake_check_binary),
                            ('group_id', lambda name: {'audio': 29, 'video': 44}[name]),
                            ('devices', lambda seconds=30: (TOUCH, DRM))):
            patcher = mock.patch.object(self.board, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    @staticmethod
    def fake_check_binary(path: Path) -> None:
        if path.read_bytes()[:4] != b'\x7fELF':
            raise AssertionError('not a binary')

    def systemctl_calls(self) -> list[str]:
        return self.calls.read_text().splitlines() if self.calls.exists() else []

    def write_config(self, **files: object) -> None:
        directory = self.prefix/'etc/house-panel'
        directory.mkdir(parents=True, exist_ok=True)
        for name, value in files.items():
            (directory/name).write_text(value if isinstance(value, str) else json.dumps(value))

    def bundle(self, label: str = 'one', tamper: str | None = None, extra: dict | None = None) -> tuple[Path, str]:
        root = Path(self.temporary.name)/f'bundle-{label}'
        shutil.rmtree(root, ignore_errors=True)
        (root/'bin').mkdir(parents=True)
        for binary in self.board.BINARIES:
            (root/'bin'/binary).write_bytes(AARCH64_ELF + label.encode())
        (root/'config').mkdir()
        (root/'config/applications.json').write_text('{}\n')
        (root/'scenarios').mkdir()
        (root/'scenarios/home.json').write_text('{}\n')
        shutil.copy2(BOARD_SCRIPT, root/'bench_board.py')
        (root/'BUILD').write_text(f'board_id=frdm-imx8mp\ntier=bench\nproject_commit={label}\n')
        for name, data in (extra or {}).items():
            (root/name).write_bytes(data)
        files = sorted(p for p in root.rglob('*') if p.is_file())
        (root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' +
                                               str(p.relative_to(root)) + '\n' for p in files))
        if tamper:
            (root/tamper).write_bytes(b'changed after hashing')
        tarball = Path(self.temporary.name)/f'bundle-{label}.tar.gz'
        with tarfile.open(tarball, 'w:gz') as archive:
            archive.add(root, arcname='.')
        return tarball, hashlib.sha256(tarball.read_bytes()).hexdigest()

    def install(self, label: str = 'one') -> dict:
        tarball, digest = self.bundle(label)
        return self.board.install(str(tarball), digest)


class InstallTest(BoardCase):
    def setUp(self) -> None:
        super().setUp()
        self.write_config(**{'bench.json': CONFIG})

    def test_install_without_a_valid_configuration_changes_nothing(self) -> None:
        (self.prefix/'etc/house-panel/bench.json').unlink()
        tarball, digest = self.bundle()
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_MISSING'):
            self.board.install(str(tarball), digest)
        self.write_config(**{'music.json': {'client_id': 'short', 'output_name': 'Bose'}, 'bench.json': CONFIG})
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_INVALID.MUSIC'):
            self.board.install(str(tarball), digest)
        self.assertFalse((self.prefix/'opt/house-panel').exists())
        self.assertEqual(self.systemctl_calls(), [], 'weston is never touched')

    def test_install_verifies_swaps_and_starts_the_target(self) -> None:
        tarball, digest = self.bundle()
        self.board.install(str(tarball), digest)
        install = self.prefix/'opt/house-panel'
        self.assertEqual(os.readlink(install/'current'), 'releases/' + digest[:16])
        self.assertFalse((install/'previous').is_symlink())
        self.assertTrue((install/'current/bin/bhp-panel-ui').is_file())
        units = self.prefix/'etc/systemd/system'
        self.assertEqual(sorted(p.name for p in units.iterdir()), sorted(self.board.unit_files()))
        record = json.loads((install/'install.json').read_text())
        self.assertEqual(record['weston_before'], {'weston.socket': 'enabled', 'weston.service': 'enabled'})
        self.assertEqual(record['build']['project_commit'], 'one')
        calls = self.systemctl_calls()
        for expected in ('daemon-reload', 'disable --now weston.service', 'mask weston.service',
                         'disable --now weston.socket', 'mask weston.socket', 'enable bhp-bench.target',
                         'restart bhp-bench.target'):
            self.assertIn(expected, calls)

    def test_a_wrong_hash_or_a_changed_file_is_refused_and_changes_nothing(self) -> None:
        self.install('one')
        before = os.readlink(self.prefix/'opt/house-panel/current')
        tarball, digest = self.bundle('two')
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.BUNDLE_HASH_MISMATCH'):
            self.board.install(str(tarball), 'f' * 64)
        tarball, digest = self.bundle('three', tamper='bin/bhp-reporter')
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.BUNDLE_FILE_HASH_MISMATCH'):
            self.board.install(str(tarball), digest)
        self.assertEqual(os.readlink(self.prefix/'opt/house-panel/current'), before)
        self.assertEqual([p.name for p in (self.prefix/'opt/house-panel/releases').iterdir()], [before.split('/')[1]])

    def test_extra_files_links_and_escapes_are_refused(self) -> None:
        tarball, digest = self.bundle('extra')
        root = Path(self.temporary.name)/'bundle-extra'
        (root/'stray').write_text('not in SHA256SUMS')
        with tarfile.open(tarball, 'w:gz') as archive:
            archive.add(root, arcname='.')
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.BUNDLE_FILES_DIFFER'):
            self.board.install(str(tarball), hashlib.sha256(tarball.read_bytes()).hexdigest())
        for member in (tarfile.TarInfo('./bin/link'), tarfile.TarInfo('../escape')):
            if member.name.endswith('link'):
                member.type, member.linkname = tarfile.SYMTYPE, '/etc/shadow'
            evil = Path(self.temporary.name)/'evil.tar.gz'
            with tarfile.open(evil, 'w:gz') as archive:
                archive.addfile(member, io.BytesIO(b''))
            with self.assertRaisesRegex(self.board.BenchError, 'BENCH.BUNDLE_MEMBER_REFUSED'):
                self.board.install(str(evil), hashlib.sha256(evil.read_bytes()).hexdigest())

    def test_a_bundle_whose_installer_differs_is_refused(self) -> None:
        tarball, digest = self.bundle('other-installer')
        root = Path(self.temporary.name)/'bundle-other-installer'
        (root/'bench_board.py').write_text('print("another installer")\n')
        files = sorted(p for p in root.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
        (root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' +
                                               str(p.relative_to(root)) + '\n' for p in files))
        with tarfile.open(tarball, 'w:gz') as archive:
            archive.add(root, arcname='.')
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.INSTALLER_DIFFERS_FROM_BUNDLE'):
            self.board.install(str(tarball), hashlib.sha256(tarball.read_bytes()).hexdigest())

    def test_the_previous_release_is_kept_and_rollback_swaps_back(self) -> None:
        install = self.prefix/'opt/house-panel'
        tarball, first = self.bundle('one')
        self.board.install(str(tarball), first)
        tarball, second = self.bundle('two')
        self.board.install(str(tarball), second)
        self.assertEqual(os.readlink(install/'current'), 'releases/' + second[:16])
        self.assertEqual(os.readlink(install/'previous'), 'releases/' + first[:16])
        self.install('three')
        self.assertEqual(len(list((install/'releases').iterdir())), 2, 'only current and previous are kept')
        self.assertEqual(os.readlink(install/'previous'), 'releases/' + second[:16])
        self.board.rollback()
        self.assertEqual(os.readlink(install/'current'), 'releases/' + second[:16])
        self.assertIn('restart bhp-bench.target', self.systemctl_calls())

    def test_rollback_without_a_previous_release_is_refused(self) -> None:
        self.install('one')
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.NO_PREVIOUS_RELEASE'):
            self.board.rollback()

    def test_uninstall_restores_weston_and_keeps_state_unless_purged(self) -> None:
        self.install('one')
        state = self.prefix/'opt/house-panel/state/music'
        state.mkdir(parents=True)
        (state/'kept').write_text('x')
        self.calls.unlink()
        self.board.uninstall(purge=False)
        self.assertFalse(any((self.prefix/'etc/systemd/system').iterdir()))
        self.assertFalse((self.prefix/'opt/house-panel/releases').exists())
        self.assertTrue((state/'kept').exists())
        calls = self.systemctl_calls()
        for expected in ('unmask weston.socket', 'unmask weston.service', 'enable weston.socket weston.service',
                         'start weston.socket weston.service', 'disable bhp-bench.target'):
            self.assertIn(expected, calls)
        self.install('two')
        self.board.uninstall(purge=True)
        self.assertFalse((self.prefix/'opt/house-panel').exists())

    def test_it_writes_only_where_adr_0014_allows(self) -> None:
        self.write_config(**{'bench.json': CONFIG, 'reporter.json': {'inbox_url': 'https://pc.example.ts.net:8743'}})
        self.install('one')
        self.board.prepare()
        allowed = ('opt/house-panel', 'etc/house-panel', 'run/house-panel', 'run/bhp-sim')
        for path in self.prefix.rglob('*'):
            relative = path.relative_to(self.prefix).as_posix()
            if path.is_dir() and any(a.startswith(relative + '/') or a == relative for a in allowed + ('etc/systemd/system',)):
                continue
            unit = relative.startswith('etc/systemd/system/bhp-bench')
            self.assertTrue(unit or any(relative == a or relative.startswith(a + '/') for a in allowed), relative)


class ConfigurationTest(BoardCase):
    def test_the_bench_configuration_is_checked_field_by_field(self) -> None:
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_MISSING'):
            self.board.bench_config()
        self.write_config(**{'bench.json': {'latitude': 40.7, 'longitude': -74.0, 'timezone': 'America/New_York'}})
        config = self.board.bench_config()
        self.assertEqual((config['rotation'], config['awake'], config['touch_presence_seconds']),
                         ('180', {'from': '06:30', 'to': '23:00'}, 300))
        for change, code in (({'latitude': 91}, 'LATITUDE'), ({'longitude': True}, 'LONGITUDE'),
                             ({'timezone': 'Mars/Olympus'}, 'TIMEZONE'), ({'timezone': '../../etc/passwd'}, 'TIMEZONE'),
                             ({'rotation': '90'}, 'ROTATION'), ({'awake': {'from': '25:00', 'to': '23:00'}}, 'AWAKE'),
                             ({'touch_presence_seconds': 0}, 'TOUCH_PRESENCE_SECONDS'), ({'token': 'x'}, 'KEYS')):
            self.write_config(**{'bench.json': {**CONFIG, **change}})
            with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_INVALID.' + code):
                self.board.bench_config()

    def test_optional_service_files_are_checked(self) -> None:
        self.write_config(**{'reporter.json': {'inbox_url': 'http://pc:8743'}})
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_INVALID.REPORTER'):
            self.board.reporter_settings()
        self.write_config(**{'music.json': {'client_id': 'abc', 'output_name': 'Bose'}})
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_INVALID.MUSIC'):
            self.board.music_settings()
        self.write_config(**{'music.json': {'client_id': '0' * 32, 'output_name': 'Bose Portable', 'secret': 'x'}})
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.CONFIG_INVALID.MUSIC'):
            self.board.music_settings()

    def test_prepare_writes_the_real_location_and_touch_presence(self) -> None:
        self.write_config(**{'bench.json': CONFIG, 'music.json': {'client_id': 'a' * 32, 'output_name': 'Bose'}})
        record = self.board.prepare()
        environment = dict(line.split('=', 1) for line in
                           (self.prefix/'run/house-panel/supervisor.env').read_text().splitlines())
        self.assertEqual(environment['BHP_PANEL_SUPERVISOR_LATITUDE'], '40.71')
        self.assertEqual(environment['BHP_PANEL_SUPERVISOR_LONGITUDE'], '-74.01')
        self.assertEqual(environment['BHP_PANEL_SUPERVISOR_TOUCH_PRESENCE_SECONDS'], '300')
        self.assertEqual(environment['BHP_PANEL_SUPERVISOR_SUSPEND_MODE'], 'disabled')
        self.assertEqual((record['music'], record['reporter'], record['timezone']), (True, False, 'America/New_York'))
        self.assertEqual(record['evidence'], 'bench_not_gate_evidence')


class ServiceTest(BoardCase):
    RUNTIME = {'touch': TOUCH, 'drm': DRM, 'rotation': '180', 'timezone': 'America/New_York', 'study_config': False,
               'reporter': True, 'music': True, 'voice_lab': False}

    def test_each_service_runs_under_its_own_uid(self) -> None:
        self.write_config(**{'reporter.json': {'inbox_url': 'https://pc.example.ts.net:8743'},
                             'music.json': {'client_id': 'b' * 32, 'output_name': 'Bose'}})
        expected = {'broker': 1103, 'reporter': 1106, 'music': 1105, 'ui': 1102}
        for name, uid in expected.items():
            command = self.board.service_command(name, self.RUNTIME)
            self.assertEqual(command[:3], ['setpriv', f'--reuid={uid}', f'--regid={uid}'], name)
            self.assertIn('--no-new-privs', command)
            self.assertFalse(any('token' in part or 'secret' in part for part in command), name)
        for name in ('pir', 'supervisor'):
            self.assertNotEqual(self.board.service_command(name, self.RUNTIME)[0], 'setpriv')
        ui = self.board.service_command('ui', self.RUNTIME)
        self.assertIn('--music-control-socket', ui)
        self.assertIn('--bug-reporter-socket', ui)
        self.assertNotIn('--voice-lab-uploader-socket', ui)
        self.assertNotIn('--study-config', ui)
        broker = self.board.service_command('broker', self.RUNTIME)
        self.assertIn('--report-handoff-dir', broker)
        self.assertIn('--groups=29,1108,1109', broker)
        self.assertIn('--groups=1110', self.board.tee_command('ui'))

    def test_an_unconfigured_service_is_refused(self) -> None:
        with self.assertRaisesRegex(self.board.BenchError, 'BENCH.SERVICE_NOT_CONFIGURED'):
            self.board.service_command('music', self.RUNTIME)

    def test_units_restart_with_backoff_and_need_their_config(self) -> None:
        units = self.board.unit_files()
        for name in self.board.SERVICES:
            text = units[f'bhp-bench-{name}.service']
            self.assertIn(f'ExecStart=/usr/bin/python3 -I /opt/house-panel/current/bench_board.py exec {name}', text)
            self.assertIn('Restart=always', text)
            self.assertIn('RestartMaxDelaySec=2min', text)
            self.assertIn('PartOf=bhp-bench.target', text)
            self.assertNotIn('User=', text)
            self.assertEqual('ConditionPathExists' in text, name in self.board.OPTIONAL, name)
        self.assertIn('WantedBy=multi-user.target', units['bhp-bench.target'])

    def test_the_awake_hours_include_the_start_and_wrap_past_midnight(self) -> None:
        def at(hour: int, minute: int) -> datetime.datetime:
            return datetime.datetime(2026, 11, 10, hour, minute)
        day = {'from': '06:30', 'to': '23:00'}
        self.assertFalse(self.board.awake(at(6, 29), day))
        self.assertTrue(self.board.awake(at(6, 30), day))
        self.assertFalse(self.board.awake(at(23, 0), day))
        night_shift = {'from': '22:00', 'to': '02:00'}
        self.assertTrue(self.board.awake(at(23, 30), night_shift))
        self.assertTrue(self.board.awake(at(1, 59), night_shift))
        self.assertFalse(self.board.awake(at(12, 0), night_shift))


class PcTest(unittest.TestCase):
    def run_pc(self, *arguments: str, target: str | None = 'root@192.0.2.10') -> tuple[int, dict]:
        environment = {k: v for k, v in os.environ.items() if k != 'BHP_BOARD_SSH_TARGET'}
        if target:
            environment['BHP_BOARD_SSH_TARGET'] = target
        result = subprocess.run([sys.executable, '-I', str(PC_SCRIPT), *arguments], capture_output=True, text=True,
                                env=environment)
        return result.returncode, json.loads(result.stdout)

    def test_dry_runs_print_the_plan_and_touch_nothing(self) -> None:
        code, plan = self.run_pc('install')
        self.assertEqual(code, 0)
        commands = plan['data']['commands']
        self.assertIn('bench_board.py install /tmp/bhp-bench-upload/frdm-bench-bundle.tar.gz', commands[2][-1])
        self.assertEqual(commands[-1][-1], 'rm -rf /tmp/bhp-bench-upload')
        code, plan = self.run_pc('uninstall', '--purge')
        self.assertTrue(plan['data']['commands'][0][-1].endswith('bench_board.py uninstall --purge'))
        code, plan = self.run_pc('build')
        self.assertEqual(code, 0)
        self.assertIn('bhp-reporter', plan['data']['commands'][1])

    def test_board_commands_need_an_exact_target_and_known_names(self) -> None:
        code, result = self.run_pc('status', '--execute', target='root@board.local')
        self.assertEqual((code, result['error']['code'].split()[0]), (1, 'BENCH_PC.SSH_TARGET_REQUIRED'))
        code, result = self.run_pc('logs', 'everything')
        self.assertEqual((code, result['error']['code']), (1, 'BENCH_PC.USAGE'))


if __name__ == '__main__':
    unittest.main()
