"""Host-side checks of the guest capture-closure matrix plan and verdicts (no ALSA needed)."""
import contextlib
import io
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'guest'))
import capture_closure_matrix as matrix


def record(message, **fields):
    base = {'message': message, 'evidence': matrix.EVIDENCE,
            'suspend_write': 'not_performed_check_only', 'termination_requested': 'false',
            'forced': 'false', 'waited_milliseconds': '0'}
    base.update(fields)
    return base


class CaptureClosureMatrixTests(unittest.TestCase):
    def setUp(self):
        self.cases = {case['name']: case for case in matrix.plan(1103, 1102)}

    def test_plan_covers_every_gate_outcome_and_starts_and_ends_closed(self):
        names = [case['name'] for case in matrix.plan(1103, 1102)]
        self.assertEqual(names[0], 'baseline_closed')
        self.assertEqual(names[-1], 'final_closed')
        expected_codes = {case['expect'].get('error_code') for case in self.cases.values()}
        self.assertLessEqual({'PANEL_SUPERVISOR.CAPTURE_CLOSURE_UNVERIFIED',
                              'PANEL_SUPERVISOR.CAPTURE_TERMINATION_UNAVAILABLE'}, expected_codes)
        self.assertEqual(self.cases['no_termination_path_refuses_suspend']['terminator_uid'], None)
        self.assertEqual(self.cases['foreign_holder_refuses_suspend']['holder']['uid'], 1102)

    def test_holder_drops_to_the_exact_uid_without_groups(self):
        command = matrix.holder_command(1103, True)
        self.assertEqual(command[:5], ['setpriv', '--reuid=1103', '--regid=1103',
                                       '--clear-groups', '--'])
        self.assertIn("trap '' TERM", command[-1])
        self.assertNotIn('trap', matrix.holder_command(1103, False)[-1])

    def test_graceful_and_forced_verdicts(self):
        graceful = self.cases['broker_holder_terminated_gracefully']
        ok = record('capture_closure_verified', termination_requested='true')
        self.assertEqual(matrix.evaluate(graceful, 0, ok, {'signal': 15,
                                                           'alive_after_check': False}), [])
        self.assertTrue(matrix.evaluate(graceful, 0, ok, {'signal': 9,
                                                          'alive_after_check': False}))
        forced = self.cases['broker_holder_ignoring_sigterm_is_killed']
        killed = record('capture_closure_verified', termination_requested='true', forced='true')
        self.assertEqual(matrix.evaluate(forced, 0, killed, {'signal': 9,
                                                             'alive_after_check': False}), [])

    def test_refusals_require_the_exact_code_deadline_and_a_surviving_holder(self):
        foreign = self.cases['foreign_holder_refuses_suspend']
        refused = record('capture_closure_refused',
                         error_code='PANEL_SUPERVISOR.CAPTURE_CLOSURE_UNVERIFIED',
                         termination_requested='true', forced='true', waited_milliseconds='2004')
        self.assertEqual(matrix.evaluate(foreign, 1, refused, {'alive_after_check': True}), [])
        early = dict(refused, waited_milliseconds='1500')
        self.assertIn('waited less than the closure deadline',
                      matrix.evaluate(foreign, 1, early, {'alive_after_check': True}))
        self.assertTrue(matrix.evaluate(foreign, 0, record('capture_closure_verified'), None))

    def test_check_output_must_be_one_check_only_simulation_record(self):
        line = json.dumps(record('capture_closure_verified'))
        self.assertEqual(matrix.parse_check_output(line + '\n')['message'],
                         'capture_closure_verified')
        with self.assertRaises(matrix.MatrixError):
            matrix.parse_check_output(line + '\n' + line)
        with self.assertRaises(matrix.MatrixError):
            matrix.parse_check_output(json.dumps(dict(record('capture_closure_verified'),
                                                      suspend_write='performed')))

    def test_dry_run_prints_the_plan_without_touching_the_host(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = matrix.main(['--check-binary', '/opt/bhp-sim/capture_closure_check'])
        self.assertEqual(status, 0)
        plan = json.loads(output.getvalue())
        self.assertEqual(plan['mode'], 'dry_run')
        self.assertEqual(plan['evidence'], 'simulation_not_hardware_evidence')
        self.assertEqual(len(plan['cases']), 6)
        self.assertEqual(plan['cases'][4]['check_command'], ['/opt/bhp-sim/capture_closure_check'])

    def test_stand_in_dry_run_is_labelled(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            matrix.main(['--check-binary', 'x', '--stand-in'])
        self.assertEqual(json.loads(output.getvalue())['capture_device'], 'stand_in_regular_file')

    def test_shared_broker_and_foreign_uids_are_rejected(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(matrix.main(['--check-binary', 'x', '--foreign-uid', '1103']), 2)


if __name__ == '__main__':
    unittest.main()
