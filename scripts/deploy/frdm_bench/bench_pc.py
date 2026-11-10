#!/usr/bin/env python3
"""PC side of the bench tier (docs/decisions/0014-run-a-bench-tier-on-the-vendor-image.md).

Usage: frdm_bench.sh build | install | rollback | uninstall [--purge] | status | logs SERVICE, then optional --execute.
Without --execute it prints the plan as JSON and touches nothing.

- build: cross-builds the bench bundle into .build/frdm-bench-bundle.tar.gz with the demo's toolchain
  (needs scripts/build/frdm_vendor_sysroot.sh --execute first, or BHP_FRDM_VENDOR_SYSROOT).
- install: copies the bundle and the board script to the board's /tmp/bhp-bench-upload, runs
  `bench_board.py install` there with the bundle's SHA-256, then removes the upload. ADR 0014
  approves this write of the board's storage; nothing else on the board changes.
- rollback, uninstall, status: run the installed bench_board.py.
- logs SERVICE: the last 300 journal lines of one bench unit.
Board commands need BHP_BOARD_SSH_TARGET (config/device.env). Each executed board command appends
to hardware_tests/results/frdm/bench-<UTC>/run.jsonl. Every record says bench_not_gate_evidence.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = 'bench_not_gate_evidence'
BUILD = ROOT/'.build/frdm-bench'
BUNDLE = ROOT/'.build/frdm-bench-bundle'
TARBALL = ROOT/'.build/frdm-bench-bundle.tar.gz'
BOARD_SCRIPT = ROOT/'scripts/deploy/frdm_bench/bench_board.py'
UPLOAD = '/tmp/bhp-bench-upload'
INSTALLED_SCRIPT = '/opt/house-panel/current/bench_board.py'
SERVICES = ('prepare', 'pir', 'presence', 'supervisor', 'reporter', 'broker', 'music', 'uploader', 'ui')
ALLOWED_NEEDED = {'libc.so.6', 'libm.so.6', 'ld-linux-aarch64.so.1', 'libdrm.so.2', 'libpng16.so.16',
                  'libz.so.1', 'libcrypto.so.3', 'libssl.so.3', 'libcurl.so.4', 'libasound.so.2'}
MAXIMUM_GLIBC = (2, 41)
TARGETS = {'bhp-panel-ui': 'panel_ui/bhp-panel-ui', 'panel_supervisord': 'panel_supervisor/panel_supervisord',
           'bhp-media-broker': 'media_broker/bhp-media-broker',
           'bhp-voice-lab-uploader': 'voice_lab_recorder/bhp-voice-lab-uploader',
           'bhp-music-control': 'music_control/bhp-music-control', 'bhp-reporter': 'bug_reporter/bhp-reporter',
           'bhp-log-tee': 'bug_reporter/bhp-log-tee'}


class PcError(Exception):
    pass


def sysroot() -> Path:
    return Path(os.environ.get('BHP_FRDM_VENDOR_SYSROOT') or ROOT/'.build/frdm-sysroot')


def build_commands() -> list[list[str]]:
    return [
        ['cmake', '-S', str(ROOT), '-B', str(BUILD), '-G', 'Ninja',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT/'cmake/toolchains/frdm-vendor-aarch64.cmake'),
         '-DCMAKE_BUILD_TYPE=Release', '-DBHP_BUILD_TESTS=OFF', '-DBHP_BUILD_PANEL_RENDERER=ON',
         '-DBHP_BUILD_PANEL_DRM=ON', '-DBHP_BUILD_HTTP_LEXINET_CLIENT=ON', '-DBHP_BUILD_MEDIA_ALSA=ON'],
        ['cmake', '--build', str(BUILD), '--target', *TARGETS],
        ['clang', '--target=aarch64-linux-gnu', '--sysroot=' + str(sysroot()), '-fuse-ld=lld', '-mcpu=cortex-a53',
         '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2', str(ROOT/'scripts/sim/guest/pir_uinput.c'),
         '-o', str(BUILD/'bhp-sim-pir')],
    ]


def check_binary(name: str, path: Path) -> dict:
    """The board supplies these libraries; anything else, or newer glibc symbols, fails."""
    dynamic = subprocess.run(['readelf', '-d', str(path)], check=True, capture_output=True, text=True).stdout
    needed = set(re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', dynamic))
    versions = subprocess.run(['readelf', '-V', str(path)], check=True, capture_output=True, text=True).stdout
    glibc = sorted({tuple(int(part) for part in match.split('.')) for match in re.findall(r'GLIBC_([0-9.]+)', versions)})
    if not needed <= ALLOWED_NEEDED:
        raise PcError('BENCH_PC.LIBRARY_NOT_ON_BOARD ' + name + ': ' + ', '.join(sorted(needed - ALLOWED_NEEDED)))
    if glibc and glibc[-1] > MAXIMUM_GLIBC:
        raise PcError('BENCH_PC.GLIBC_TOO_NEW ' + name)
    return {'needed': sorted(needed), 'maximum_glibc': '.'.join(map(str, glibc[-1])) if glibc else None}


def git(*arguments: str) -> str:
    return subprocess.run(['git', '-C', str(ROOT), *arguments], check=True, capture_output=True, text=True).stdout.strip()


def assemble(binaries: dict[str, Path]) -> str:
    """Writes the bundle directory and tarball; returns the tarball's SHA-256."""
    shutil.rmtree(BUNDLE, ignore_errors=True)
    (BUNDLE/'bin').mkdir(parents=True)
    for name, path in binaries.items():
        shutil.copy2(path, BUNDLE/'bin'/name)
    shutil.copytree(ROOT/'simulator/scenarios', BUNDLE/'scenarios')
    (BUNDLE/'config').mkdir()
    shutil.copy2(ROOT/'panel_ui/config/applications.json', BUNDLE/'config/applications.json')
    shutil.copy2(BOARD_SCRIPT, BUNDLE/'bench_board.py')
    dirty = bool(git('status', '--porcelain'))
    # key=value lines: bhp-reporter's --build-info reads them into every report (report_bundle.cpp).
    (BUNDLE/'BUILD').write_text(f'board_id=frdm-imx8mp\ntier=bench\nproject_commit={git("rev-parse", "HEAD")}\n'
                                f'tree={"dirty" if dirty else "clean"}\n'
                                f'built_at={time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}\n'
                                f'evidence={EVIDENCE}\n')
    files = sorted(path for path in BUNDLE.rglob('*') if path.is_file())
    (BUNDLE/'SHA256SUMS').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' +
                                             str(path.relative_to(BUNDLE)) + '\n' for path in files))
    with tarfile.open(TARBALL, 'w:gz') as archive:
        archive.add(BUNDLE, arcname='.')
    return hashlib.sha256(TARBALL.read_bytes()).hexdigest()


def build(execute: bool) -> dict:
    commands = build_commands()
    plan = {'command': 'build', 'execute': execute, 'commands': commands, 'bundle': str(TARBALL)}
    if not execute:
        return plan
    if not (sysroot()/'usr/lib/aarch64-linux-gnu/libc.so.6').exists():
        raise PcError('BENCH_PC.SYSROOT_MISSING run scripts/build/frdm_vendor_sysroot.sh --execute')
    environment = os.environ | {'BHP_FRDM_VENDOR_SYSROOT': str(sysroot())}  # the toolchain file reads it
    for command in commands:
        subprocess.run(command, cwd=ROOT, check=True, env=environment)
    binaries = {name: BUILD/relative for name, relative in TARGETS.items()} | {'bhp-sim-pir': BUILD/'bhp-sim-pir'}
    plan['binaries'] = {name: check_binary(name, path) for name, path in binaries.items()}
    plan['bundle_sha256'] = assemble(binaries)
    return plan


def ssh_target() -> str:
    target = os.environ.get('BHP_BOARD_SSH_TARGET', '')
    if not re.fullmatch(r'root@[0-9.]+', target):
        raise PcError('BENCH_PC.SSH_TARGET_REQUIRED export BHP_BOARD_SSH_TARGET from config/device.env')
    return target


def ssh(target: str, remote: str) -> list[str]:
    return ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', target, remote]


def board_commands(command: str, arguments: list[str], target: str) -> list[list[str]]:
    if command == 'install':
        sha = hashlib.sha256(TARBALL.read_bytes()).hexdigest() if TARBALL.exists() else '<bundle-sha256>'
        return [ssh(target, f'rm -rf {UPLOAD} && mkdir -m 700 {UPLOAD}'),
                ['scp', '-q', '-o', 'BatchMode=yes', str(TARBALL), str(BOARD_SCRIPT), f'{target}:{UPLOAD}/'],
                ssh(target, f'python3 -I {UPLOAD}/bench_board.py install {UPLOAD}/{TARBALL.name} {sha}'),
                ssh(target, f'rm -rf {UPLOAD}')]
    if command in ('rollback', 'status') and not arguments:
        return [ssh(target, f'python3 -I {INSTALLED_SCRIPT} {command}')]
    if command == 'uninstall' and arguments in ([], ['--purge']):
        return [ssh(target, f'python3 -I {INSTALLED_SCRIPT} uninstall {" ".join(arguments)}'.rstrip())]
    if command == 'logs' and len(arguments) == 1 and arguments[0] in SERVICES:
        return [ssh(target, f'journalctl -u bhp-bench-{arguments[0]}.service -n 300 --no-pager -o short-iso')]
    raise PcError('BENCH_PC.USAGE')


def board(command: str, arguments: list[str], execute: bool) -> dict:
    target = ssh_target() if execute else os.environ.get('BHP_BOARD_SSH_TARGET', 'root@<board>')
    commands = board_commands(command, arguments, target)
    plan = {'command': command, 'execute': execute, 'commands': commands}
    if not execute:
        return plan
    if command == 'install' and not TARBALL.exists():
        raise PcError('BENCH_PC.BUNDLE_MISSING run build --execute first')
    results = ROOT/'hardware_tests/results/frdm'/('bench-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()))
    results.mkdir(parents=True)
    outputs = []
    for step in commands:
        finished = subprocess.run(step, capture_output=True, text=True)
        entry = {'command': step, 'returncode': finished.returncode, 'stdout': finished.stdout[-20000:],
                 'stderr': finished.stderr[-4000:], 'evidence': EVIDENCE}
        with (results/'run.jsonl').open('a') as log:
            log.write(json.dumps(entry) + '\n')
        outputs.append(entry)
        if finished.returncode != 0:
            raise PcError(f'BENCH_PC.BOARD_COMMAND_FAILED see {results}/run.jsonl')
    plan['results'] = str(results)
    plan['output'] = outputs[-2 if command == 'install' else -1]['stdout'].strip()
    return plan


def main(arguments: list[str]) -> dict:
    execute = bool(arguments) and arguments[-1] == '--execute'
    arguments = arguments[:-1] if execute else arguments
    if arguments == ['build']:
        return build(execute)
    if arguments and arguments[0] in ('install', 'rollback', 'uninstall', 'status', 'logs'):
        return board(arguments[0], arguments[1:], execute)
    raise PcError('BENCH_PC.USAGE')


if __name__ == '__main__':
    try:
        print(json.dumps({'data': main(sys.argv[1:]), 'error': None, 'evidence': EVIDENCE}, indent=2))
    except (PcError, OSError, subprocess.SubprocessError) as error:
        print(json.dumps({'data': None, 'error': {'code': str(error)}, 'evidence': EVIDENCE}))
        raise SystemExit(1)
