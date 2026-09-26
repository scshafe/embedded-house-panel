#!/usr/bin/env python3
"""Guest-only capture-closure matrix against a real snd-aloop capture device.

Runs as root inside ADR 0009's disposable Debian guest. Each case holds one real ALSA
capture substream open under an exact numeric UID, then runs the check-only
`capture_closure_check` binary: the daemon's CaptureClosureGatedSuspend with an inner
suspend that never writes /sys/power/state. Dry run by default (prints the plan);
`--execute` runs it and writes the JSON report. Every result is
simulation_not_hardware_evidence; microphone rail-off stays an FRDM gate.
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path

EVIDENCE = 'simulation_not_hardware_evidence'
SCHEMA = 'bhp.capture_closure_matrix.v1'
DEFAULT_OUTPUT = Path('/var/lib/bhp-sim/capture-closure-matrix.json')
HOLD_SECONDS = '600'


class MatrixError(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(f'CAPTURE_MATRIX.{code}: {detail}')
        self.code = code


def plan(broker_uid: int, foreign_uid: int) -> list[dict]:
    """The ordered cases. `holder` is who holds capture open; `terminator_uid` is the gate's."""
    return [
        {'name': 'baseline_closed', 'holder': None, 'terminator_uid': broker_uid,
         'expect': {'result': 'verified', 'termination_requested': 'false'}},
        {'name': 'broker_holder_terminated_gracefully',
         'holder': {'uid': broker_uid, 'ignore_sigterm': False}, 'terminator_uid': broker_uid,
         'expect': {'result': 'verified', 'termination_requested': 'true', 'forced': 'false',
                    'holder_signal': 15}},
        {'name': 'broker_holder_ignoring_sigterm_is_killed',
         'holder': {'uid': broker_uid, 'ignore_sigterm': True}, 'terminator_uid': broker_uid,
         'expect': {'result': 'verified', 'termination_requested': 'true', 'forced': 'true',
                    'holder_signal': 9}},
        {'name': 'foreign_holder_refuses_suspend',
         'holder': {'uid': foreign_uid, 'ignore_sigterm': False}, 'terminator_uid': broker_uid,
         'expect': {'result': 'refused', 'error_code': 'PANEL_SUPERVISOR.CAPTURE_CLOSURE_UNVERIFIED',
                    'holder_alive': True, 'minimum_waited_milliseconds': 2000}},
        {'name': 'no_termination_path_refuses_suspend',
         'holder': {'uid': broker_uid, 'ignore_sigterm': False}, 'terminator_uid': None,
         'expect': {'result': 'refused',
                    'error_code': 'PANEL_SUPERVISOR.CAPTURE_TERMINATION_UNAVAILABLE',
                    'holder_alive': True}},
        {'name': 'final_closed', 'holder': None, 'terminator_uid': broker_uid,
         'expect': {'result': 'verified', 'termination_requested': 'false'}},
    ]


def check_command(binary: str, terminator_uid: int | None) -> list[str]:
    command = [binary]
    if terminator_uid is not None:
        command += ['--media-broker-uid', str(terminator_uid)]
    return command


def holder_command(uid: int, ignore_sigterm: bool) -> list[str]:
    """Drops to the exact UID with no supplementary groups; the capture fd is inherited."""
    body = ("trap '' TERM; exec sleep " if ignore_sigterm else 'exec sleep ') + HOLD_SECONDS
    return ['setpriv', f'--reuid={uid}', f'--regid={uid}', '--clear-groups', '--',
            'sh', '-c', body]


def parse_check_output(stdout: str) -> dict:
    lines = [line for line in stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise MatrixError('CHECK_OUTPUT_INVALID', f'expected one JSON line, got {len(lines)}')
    record = json.loads(lines[0])
    if record.get('evidence') != EVIDENCE or record.get('suspend_write') != 'not_performed_check_only':
        raise MatrixError('CHECK_OUTPUT_INVALID', 'missing evidence or check-only markers')
    return record


def evaluate(case: dict, exit_status: int, record: dict, holder: dict | None) -> list[str]:
    """Returns the failed expectations; empty means the case passed."""
    expect = case['expect']
    failures = []
    verified = record.get('message') == 'capture_closure_verified'
    result = 'verified' if verified and exit_status == 0 else (
        'refused' if record.get('message') == 'capture_closure_refused' and exit_status == 1
        else 'invalid')
    if result != expect['result']:
        failures.append(f'result {result} != {expect["result"]}')
    for key in ('termination_requested', 'forced', 'error_code'):
        if key in expect and record.get(key) != expect[key]:
            failures.append(f'{key} {record.get(key)} != {expect[key]}')
    if 'minimum_waited_milliseconds' in expect and int(
            record.get('waited_milliseconds', '0')) < expect['minimum_waited_milliseconds']:
        failures.append('waited less than the closure deadline')
    if 'holder_signal' in expect and (holder or {}).get('signal') != expect['holder_signal']:
        failures.append(f'holder signal {(holder or {}).get("signal")} != {expect["holder_signal"]}')
    if 'holder_alive' in expect and (holder or {}).get('alive_after_check') != expect['holder_alive']:
        failures.append('holder liveness mismatch')
    return failures


def loopback_capture_node() -> tuple[int, Path]:
    cards = Path('/proc/asound/cards').read_text() if Path('/proc/asound/cards').exists() else ''
    match = re.search(r'^\s*(\d+)\s+\[Loopback\s*\]', cards, re.M)
    if not match:
        raise MatrixError('LOOPBACK_CARD_MISSING', 'no Loopback card in /proc/asound/cards')
    card = int(match.group(1))
    node = Path(f'/dev/snd/pcmC{card}D0c')
    for _ in range(50):
        if node.exists():
            return card, node
        time.sleep(0.1)
    raise MatrixError('CAPTURE_NODE_MISSING', str(node))


def substream_open(card: int | None) -> bool:
    if card is None:
        return True  # Stand-in node: there is no kernel substream to observe.
    return any(path.read_text().strip() != 'closed'
               for path in Path(f'/proc/asound/card{card}/pcm0c').glob('sub*/status'))


def start_holder(node: Path, spec: dict, card: int | None) -> subprocess.Popen:
    descriptor = os.open(node, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        process = subprocess.Popen(holder_command(spec['uid'], spec['ignore_sigterm']),
                                   pass_fds=(descriptor,), stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    finally:
        os.close(descriptor)
    for _ in range(50):
        status = Path(f'/proc/{process.pid}/status')
        if status.exists() and re.search(rf'^Uid:\s+{spec["uid"]}\s', status.read_text(), re.M) \
                and 'sleep' in Path(f'/proc/{process.pid}/comm').read_text():
            break
        time.sleep(0.1)
    else:
        process.kill()
        raise MatrixError('HOLDER_NOT_READY', f'uid {spec["uid"]}')
    if not substream_open(card):
        process.kill()
        raise MatrixError('HOLDER_CAPTURE_NOT_OPEN', f'card{card}/pcm0c')
    return process


def run_case(case: dict, binary: str, node: Path, card: int | None) -> dict:
    holder = None
    process = start_holder(node, case['holder'], card) if case['holder'] else None
    completed = subprocess.run(check_command(binary, case['terminator_uid']),
                               capture_output=True, text=True, timeout=30)
    record = parse_check_output(completed.stdout)
    if process is not None:
        alive = process.poll() is None
        if alive:
            holder = {'uid': case['holder']['uid'], 'alive_after_check': True}
            process.kill()
            process.wait(timeout=10)
        else:
            holder = {'uid': case['holder']['uid'], 'alive_after_check': False,
                      'signal': -process.returncode if process.returncode < 0 else None}
    failures = evaluate(case, completed.returncode, record, holder)
    return {'name': case['name'], 'exit_status': completed.returncode, 'check': record,
            'holder': holder, 'failures': failures, 'pass': not failures}


STAND_IN_NODE = Path('/dev/snd/pcmC99D0c')


def execute(binary: str, broker_uid: int, foreign_uid: int, output: Path,
            stand_in: bool = False) -> dict:
    """`stand_in` replaces snd-aloop with a regular file at a capture-node path: it proves
    holder discovery and the termination path where no ALSA exists, never substream closure."""
    if os.geteuid() != 0:
        raise MatrixError('ROOT_REQUIRED', 'run inside the guest as root')
    if not os.access(binary, os.X_OK):
        raise MatrixError('CHECK_BINARY_MISSING', binary)
    cleanup = []
    if stand_in:
        if STAND_IN_NODE.exists() or Path('/proc/asound').exists():
            raise MatrixError('STAND_IN_REFUSED', 'ALSA or the stand-in node already exists')
        created_directory = not STAND_IN_NODE.parent.exists()
        STAND_IN_NODE.parent.mkdir(exist_ok=True)
        STAND_IN_NODE.touch(mode=0o600)
        cleanup.append(lambda: STAND_IN_NODE.unlink(missing_ok=True))
        if created_directory:
            cleanup.append(STAND_IN_NODE.parent.rmdir)
        card, node = None, STAND_IN_NODE
    else:
        already_loaded = Path('/sys/module/snd_aloop').exists()
        if not already_loaded and subprocess.run(['modprobe', 'snd-aloop'],
                                                 capture_output=True).returncode != 0:
            raise MatrixError('SND_ALOOP_UNAVAILABLE',
                              'modprobe snd-aloop failed; the guest needs the generic arm64 kernel')
        if not already_loaded:
            cleanup.append(lambda: subprocess.run(['modprobe', '-r', 'snd-aloop'],
                                                  capture_output=True))
    try:
        if not stand_in:
            card, node = loopback_capture_node()
        cases = [run_case(case, binary, node, card) for case in plan(broker_uid, foreign_uid)]
    finally:
        for action in cleanup:
            action()
    report = {'schema': SCHEMA, 'evidence': EVIDENCE, 'kernel': platform.release(),
              'capture_device': 'stand_in_regular_file' if stand_in else 'snd_aloop',
              'kernel_substream_status': 'not_exercised' if stand_in else 'exercised',
              'capture_node': str(node), 'broker_uid': broker_uid, 'foreign_uid': foreign_uid,
              'cases': cases,
              'result': 'pass' if all(case['pass'] for case in cases) else 'fail'}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--check-binary', required=True)
    parser.add_argument('--broker-uid', type=int, default=1103)
    parser.add_argument('--foreign-uid', type=int, default=1102)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--stand-in', action='store_true',
                        help='no ALSA: hold a regular file at a capture-node path instead')
    arguments = parser.parse_args(argv)
    if arguments.broker_uid <= 0 or arguments.foreign_uid <= 0 or \
            arguments.broker_uid == arguments.foreign_uid:
        print(json.dumps({'error': 'CAPTURE_MATRIX.UID_INVALID'}), file=sys.stderr)
        return 2
    if not arguments.execute:
        print(json.dumps({'schema': SCHEMA, 'mode': 'dry_run', 'evidence': EVIDENCE,
                          'capture_device': 'stand_in_regular_file' if arguments.stand_in
                          else 'snd_aloop',
                          'cases': [{**case, 'check_command': check_command(
                              arguments.check_binary, case['terminator_uid']),
                              'holder_command': holder_command(case['holder']['uid'],
                                                               case['holder']['ignore_sigterm'])
                              if case['holder'] else None}
                              for case in plan(arguments.broker_uid, arguments.foreign_uid)],
                          'output': str(arguments.output)}, sort_keys=True))
        return 0
    try:
        report = execute(arguments.check_binary, arguments.broker_uid, arguments.foreign_uid,
                         arguments.output, arguments.stand_in)
    except MatrixError as error:
        print(json.dumps({'error': str(error), 'code': error.code}), file=sys.stderr)
        return 1
    print(json.dumps({'result': report['result'], 'output': str(arguments.output),
                      'cases': {case['name']: case['pass'] for case in report['cases']}},
                     sort_keys=True))
    return 0 if report['result'] == 'pass' else 1


if __name__ == '__main__':
    sys.exit(main())
