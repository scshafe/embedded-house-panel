"""Host checks for the FRDM panel UI demo runner (plan 2026-10-18-frdm-panel-ui-demo, Step 3)."""
import ast
import importlib.util
import json
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RUNNER = ROOT/'scripts/deploy/frdm_panel_demo.sh'
BOARD = ROOT/'scripts/deploy/frdm_panel_demo_board.py'


def load_board():
    spec = importlib.util.spec_from_file_location('frdm_panel_demo_board', BOARD)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def dry_run(*arguments, target='root@192.0.2.10'):
    environment = dict(os.environ, BHP_BOARD_SSH_TARGET=target)
    result = subprocess.run([str(RUNNER), *arguments], capture_output=True, text=True, env=environment)
    return result.returncode, json.loads(result.stdout.strip().splitlines()[-1])


class BoardWritesStayInTmpfs(unittest.TestCase):
    def test_board_roots_are_tmp_and_run(self):
        board = load_board()
        self.assertEqual(board.ROOT, Path('/tmp/bhp-demo'))
        self.assertEqual(board.RUN, Path('/run/bhp-sim'))
        self.assertEqual(board.WRITABLE_ROOTS, (board.ROOT, board.RUN))
        for path in (board.STATE, board.LOGS):
            self.assertTrue(path.is_relative_to(board.ROOT), path)

    def test_every_absolute_path_literal_is_tmpfs_or_a_device_interface(self):
        allowed = ('/tmp/bhp-demo', '/run/bhp-sim', '/sys/class/', '/dev/')
        literals = [node.value for node in ast.walk(ast.parse(BOARD.read_text()))
                    if isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value.startswith('/')]
        self.assertTrue(literals)
        for literal in literals:
            self.assertTrue(literal.startswith(allowed), literal)

    def test_ui_command_requires_rotation_and_runs_from_the_bundle(self):
        board = load_board()
        record = {'touch': {'x_min': 0, 'x_max': 799, 'y_min': 0, 'y_max': 479},
                  'drm': {'path': '/dev/dri/card1', 'connector_id': 40}, 'rotation': '180'}
        command = board.ui_command(record)
        self.assertEqual(command[0], '/tmp/bhp-demo/bin/bhp-panel-ui')
        self.assertEqual(command[command.index('--display-rotation') + 1], '180')
        self.assertEqual(command[command.index('--drm-connector-id') + 1], '40')

    def test_ui_drops_to_the_unprivileged_uid_without_a_passwd_entry(self):
        source = BOARD.read_text()
        self.assertNotIn("f'User={UI_UID}'", source)
        self.assertIn("'setpriv', f'--reuid={UI_UID}', f'--regid={UI_UID}'", source)
        self.assertIn("'--no-new-privs'", source)


class RunnerDryRun(unittest.TestCase):
    def test_start_writes_only_into_tmp_bhp_demo(self):
        code, plan = dry_run('start', '180')
        self.assertEqual(code, 0)
        self.assertFalse(plan['execute'])
        unpack, launch = (command[-1] for command in plan['commands'])
        self.assertIn('mkdir -m 755 /tmp/bhp-demo', unpack)
        self.assertIn('tar -xzf - -C /tmp/bhp-demo', unpack)
        self.assertTrue(unpack.startswith('test ! -e /tmp/bhp-demo'))
        self.assertEqual(launch, 'python3 /tmp/bhp-demo/frdm_panel_demo_board.py start --rotation 180')

    def test_rotation_must_be_0_or_180(self):
        code, plan = dry_run('start', '90')
        self.assertEqual(code, 1)
        self.assertEqual(plan['error']['code'], 'FRDM_DEMO.USAGE')

    def test_board_commands_need_an_explicit_target(self):
        code, plan = dry_run('status', target='')
        self.assertEqual(code, 1)
        self.assertEqual(plan['error']['code'], 'FRDM_DEMO.SSH_TARGET_REQUIRED')

    def test_backlight_samples_is_bounded_and_runs_the_board_probe(self):
        code, plan = dry_run('backlight-samples', '3000')
        self.assertEqual(code, 0)
        self.assertEqual(plan['commands'][-1][-1], 'python3 /tmp/bhp-demo/frdm_panel_demo_board.py backlight-samples 3000')
        for count in ('0', '6001x', '-1', 'ten'):
            self.assertEqual(dry_run('backlight-samples', count)[0], 1, count)
        board = load_board()
        for count in ('0', '6001', '1e3'):
            with self.assertRaises(ValueError):
                board.backlight_samples(count)

    def test_sample_summary_counts_each_blank_once(self):
        summary = load_board().summarize_samples([179, 0, 0, 179, 179, 0, 179, 31])
        self.assertEqual(summary, {'samples': 8, 'counts': {'179': 4, '0': 3, '31': 1}, 'zero_runs': 2})

    def test_build_dry_run_cross_compiles_with_the_vendor_toolchain(self):
        code, plan = dry_run('build')
        self.assertEqual(code, 0)
        configure = plan['commands'][0]
        self.assertIn('-DBHP_BUILD_PANEL_DRM=ON', configure)
        self.assertTrue(any(item.endswith('cmake/toolchains/frdm-vendor-aarch64.cmake') for item in configure))


if __name__ == '__main__':
    unittest.main()
