#!/bin/sh
# Builds and runs the FRDM panel UI demo (.claude/plans/2026-10-18-frdm-panel-ui-demo.md).
# Inputs: build | start 0|180 [--study-config FILE] [--voice-lab-config FILE] |
#   status | presence 0|1 | scenario NAME | screenshot NAME | backlight-samples N |
#   restart-ui | stop, then optional --execute.
#   --study-config copies a vocabulary-panel.env (no credential; ADR 0010) to the
#   board's /tmp/bhp-demo/config and runs the live study client from it.
#   --voice-lab-config copies a voice-lab.json (issuer, api_url, endpoint,
#   tailnet_suffix and, once measured, board_profile; no credential) there and
#   runs bhp-voice-lab-uploader under its own UID (issue #23). Keep it out of Git:
#   it names the tailnet.
#   restart-ui restarts only the UI unit, keeping its study state.
#   backlight-samples reads the backlight node N (1-6000) times, 10 ms apart. Default is a JSON dry run.
#   Board commands need BHP_BOARD_SSH_TARGET (config/device.env or dev.toml ssh).
# Side effects with --execute:
#   build: .build/frdm-demo/ and .build/frdm-demo-bundle.tar.gz (needs
#     scripts/build/frdm_vendor_sysroot.sh --execute first).
#   start: copies the bundle to the board's /tmp/bhp-demo, stops weston and
#     starts the demo there (scripts/deploy/frdm_panel_demo_board.py); writes
#     nothing to board storage. Opens hardware_tests/results/frdm/panel-demo-<UTC>/.
#   stop: fetches the board's demo logs and screenshots into that directory,
#     then stops the demo, removes /tmp/bhp-demo and starts weston.
# Every record says demo_not_gate_evidence; nothing here closes an FRDM gate.
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_directory/../.." && pwd)
exec python3 -I - "$project_root" "$@" <<'PY'
import json, hashlib, os, re, shutil, subprocess, sys, tarfile, time
from pathlib import Path
root = Path(sys.argv[1]); arguments = sys.argv[2:]
execute = bool(arguments) and arguments[-1] == '--execute'
if execute:
    arguments = arguments[:-1]
EVIDENCE = 'demo_not_gate_evidence'
BUILD = root/'.build/frdm-demo'
BUNDLE = root/'.build/frdm-demo-bundle'
TARBALL = root/'.build/frdm-demo-bundle.tar.gz'
SYSROOT = Path(os.environ.get('BHP_FRDM_VENDOR_SYSROOT') or root/'.build/frdm-sysroot')
CURRENT_RUN = root/'.build/frdm-demo-current-run'
BOARD_ROOT = '/tmp/bhp-demo'
BOARD_SCRIPT = BOARD_ROOT + '/frdm_panel_demo_board.py'
ALLOWED_NEEDED = {'libc.so.6', 'libm.so.6', 'ld-linux-aarch64.so.1', 'libdrm.so.2', 'libpng16.so.16',
                  'libz.so.1', 'libcrypto.so.3', 'libssl.so.3', 'libcurl.so.4', 'libasound.so.2'}
MAXIMUM_GLIBC = (2, 41)
BINARIES = {'bhp-panel-ui': BUILD/'panel_ui/bhp-panel-ui', 'panel_supervisord': BUILD/'panel_supervisor/panel_supervisord',
            'bhp-sim-pir': BUILD/'bhp-sim-pir', 'bhp-media-capture-check': BUILD/'media_broker/bhp-media-capture-check',
            'bhp-media-broker': BUILD/'media_broker/bhp-media-broker',
            'bhp-voice-lab-uploader': BUILD/'voice_lab_recorder/bhp-voice-lab-uploader'}

def fail(code, detail=''):
    print(json.dumps({'data': None, 'error': {'code': code, 'detail': detail}, 'evidence': EVIDENCE}))
    raise SystemExit(1)

def build_commands():
    return [
        ['cmake', '-S', str(root), '-B', str(BUILD), '-G', 'Ninja',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(root/'cmake/toolchains/frdm-vendor-aarch64.cmake'),
         '-DCMAKE_BUILD_TYPE=Release', '-DBHP_BUILD_TESTS=OFF', '-DBHP_BUILD_PANEL_RENDERER=ON',
         '-DBHP_BUILD_PANEL_DRM=ON', '-DBHP_BUILD_HTTP_LEXINET_CLIENT=ON',
         '-DBHP_BUILD_MEDIA_ALSA=ON'],
        ['cmake', '--build', str(BUILD), '--target', 'bhp-panel-ui', 'panel_supervisord', 'bhp-media-capture-check',
         'bhp-media-broker', 'bhp-voice-lab-uploader'],
        ['clang', '--target=aarch64-linux-gnu', '--sysroot=' + str(SYSROOT), '-fuse-ld=lld', '-mcpu=cortex-a53',
         '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2', str(root/'scripts/sim/guest/pir_uinput.c'),
         '-o', str(BINARIES['bhp-sim-pir'])],
    ]

def check_binary(name, path):
    """The board supplies these libraries; anything else, or newer glibc symbols, fails."""
    dynamic = subprocess.run(['readelf', '-d', str(path)], check=True, capture_output=True, text=True).stdout
    needed = set(re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', dynamic))
    versions = subprocess.run(['readelf', '-V', str(path)], check=True, capture_output=True, text=True).stdout
    glibc = sorted({tuple(int(part) for part in match.split('.')) for match in re.findall(r'GLIBC_([0-9.]+)', versions)})
    if not needed <= ALLOWED_NEEDED:
        fail('FRDM_DEMO.LIBRARY_NOT_ON_BOARD', name + ': ' + ', '.join(sorted(needed - ALLOWED_NEEDED)))
    if glibc and glibc[-1] > MAXIMUM_GLIBC:
        fail('FRDM_DEMO.GLIBC_TOO_NEW', name + ': ' + '.'.join(map(str, glibc[-1])))
    return {'needed': sorted(needed), 'maximum_glibc': '.'.join(map(str, glibc[-1])) if glibc else None}

def build():
    commands = build_commands()
    plan = {'command': 'build', 'execute': execute, 'commands': commands, 'bundle': str(TARBALL), 'evidence': EVIDENCE}
    if not execute:
        return plan
    if not (SYSROOT/'usr/lib/aarch64-linux-gnu/libc.so.6').exists():
        fail('FRDM_DEMO.SYSROOT_MISSING', 'run scripts/build/frdm_vendor_sysroot.sh --execute')
    for command in commands:
        subprocess.run(command, cwd=root, check=True)
    checks = {name: check_binary(name, path) for name, path in BINARIES.items()}
    shutil.rmtree(BUNDLE, ignore_errors=True)
    (BUNDLE/'bin').mkdir(parents=True)
    for name, path in BINARIES.items():
        shutil.copy2(path, BUNDLE/'bin'/name)
    shutil.copytree(root/'simulator/scenarios', BUNDLE/'scenarios')
    (BUNDLE/'config').mkdir()
    shutil.copy2(root/'panel_ui/config/applications.json', BUNDLE/'config/applications.json')
    shutil.copy2(root/'scripts/deploy/frdm_panel_demo_board.py', BUNDLE/'frdm_panel_demo_board.py')
    files = sorted(path for path in BUNDLE.rglob('*') if path.is_file())
    (BUNDLE/'SHA256SUMS').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' +
                                             str(path.relative_to(BUNDLE)) + '\n' for path in files))
    with tarfile.open(TARBALL, 'w:gz') as archive:
        archive.add(BUNDLE, arcname='.')
    plan.update({'binaries': checks, 'bundle_sha256': hashlib.sha256(TARBALL.read_bytes()).hexdigest()})
    return plan

def ssh_target():
    target = os.environ.get('BHP_BOARD_SSH_TARGET', '')
    if not re.fullmatch(r'root@[0-9.]+', target):
        fail('FRDM_DEMO.SSH_TARGET_REQUIRED', 'export BHP_BOARD_SSH_TARGET, for example root@192.168.50.19')
    return target

def ssh(target, remote):
    return ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', target, remote]

def results_directory(create):
    if create:
        directory = root/'hardware_tests/results/frdm'/('panel-demo-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()))
        directory.mkdir(parents=True)
        CURRENT_RUN.write_text(str(directory) + '\n')
        return directory
    if not CURRENT_RUN.exists():
        fail('FRDM_DEMO.NO_CURRENT_RUN', 'run start first')
    return Path(CURRENT_RUN.read_text().strip())

def record(directory, entry):
    with (directory/'run.jsonl').open('a') as log:
        log.write(json.dumps(entry) + '\n')

def study_config_file(path):
    file = Path(path)
    if not file.is_file() or file.stat().st_size > 65536:
        fail('FRDM_DEMO.STUDY_CONFIG_INVALID', 'a readable file of at most 64 KiB')
    return file

def voice_lab_config_file(path):
    file = Path(path)
    if not file.is_file() or file.stat().st_size > 65536:
        fail('FRDM_DEMO.VOICE_LAB_CONFIG_INVALID', 'a readable file of at most 64 KiB')
    try:
        settings = json.loads(file.read_text())
    except ValueError:
        fail('FRDM_DEMO.VOICE_LAB_CONFIG_INVALID', 'not JSON')
    if not isinstance(settings, dict) or not {'issuer', 'api_url', 'endpoint', 'tailnet_suffix'} <= set(settings):
        fail('FRDM_DEMO.VOICE_LAB_CONFIG_INVALID', 'needs issuer, api_url, endpoint and tailnet_suffix')
    return file

def board(remote_arguments, start=False, study_config=None, voice_lab_config=None):
    target = ssh_target()
    if study_config:
        remote_arguments = remote_arguments + ['--study-config']
    if voice_lab_config:
        remote_arguments = remote_arguments + ['--voice-lab']
    remote = 'python3 ' + BOARD_SCRIPT + ' ' + ' '.join(remote_arguments)
    commands = []
    if start:
        commands.append(ssh(target, 'test ! -e ' + BOARD_ROOT + ' && mkdir -m 755 ' + BOARD_ROOT +
                            ' && tar -xzf - -C ' + BOARD_ROOT + ' && cd ' + BOARD_ROOT + ' && sha256sum -c --quiet SHA256SUMS'))
    if study_config:
        commands.append(ssh(target, 'umask 077 && cat > ' + BOARD_ROOT + '/config/vocabulary-panel.env'))
    if voice_lab_config:
        commands.append(ssh(target, 'umask 077 && cat > ' + BOARD_ROOT + '/config/voice-lab.json'))
    commands.append(ssh(target, remote))
    plan = {'command': remote_arguments[0], 'execute': execute, 'commands': commands, 'evidence': EVIDENCE}
    if not execute:
        return plan
    if start and not TARBALL.exists():
        fail('FRDM_DEMO.BUNDLE_MISSING', 'run build --execute first')
    directory = results_directory(start)
    if start:
        with TARBALL.open('rb') as bundle:
            subprocess.run(commands[0], stdin=bundle, check=True)
    copies = [file for file in (study_config, voice_lab_config) if file]
    for offset, file in enumerate(copies):
        with file.open('rb') as configuration:
            subprocess.run(commands[-1 - len(copies) + offset], stdin=configuration, check=True)
    if remote_arguments == ['stop']:
        with (directory/'board-demo.tar.gz').open('wb') as fetched:
            subprocess.run(ssh(target, 'tar -czf - -C ' + BOARD_ROOT + ' logs state'), stdout=fetched, check=False)
    result = subprocess.run(commands[-1], capture_output=True, text=True)
    output = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else ''
    try:
        data = json.loads(output)
    except ValueError:
        data = {'data': None, 'error': {'code': 'FRDM_DEMO.BOARD_OUTPUT_INVALID', 'detail': (result.stdout + result.stderr)[-2000:]}}
    record(directory, {'time': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'arguments': remote_arguments,
                       'exit': result.returncode, 'result': data, 'evidence': EVIDENCE})
    plan.update({'results': str(directory), 'board': data})
    if result.returncode != 0:
        print(json.dumps(plan))
        raise SystemExit(1)
    return plan

def main():
    if arguments == ['build']:
        return build()
    if len(arguments) == 2 and arguments[0] == 'start' and arguments[1] in ('0', '180'):
        return board(['start', '--rotation', arguments[1]], start=True)
    if len(arguments) in (4, 6) and arguments[0] == 'start' and arguments[1] in ('0', '180'):
        flags = dict(zip(arguments[2::2], arguments[3::2]))
        if len(flags) * 2 == len(arguments) - 2 and set(flags) <= {'--study-config', '--voice-lab-config'}:
            return board(['start', '--rotation', arguments[1]], start=True,
                         study_config=study_config_file(flags['--study-config']) if '--study-config' in flags else None,
                         voice_lab_config=voice_lab_config_file(flags['--voice-lab-config'])
                         if '--voice-lab-config' in flags else None)
    if arguments in (['status'], ['stop'], ['restart-ui']):
        return board(arguments)
    if len(arguments) == 2 and arguments[0] == 'presence' and arguments[1] in ('0', '1'):
        return board(arguments)
    if len(arguments) == 2 and arguments[0] == 'backlight-samples' and re.fullmatch('[1-9][0-9]{0,3}', arguments[1]):
        return board(arguments)
    if len(arguments) == 2 and arguments[0] in ('scenario', 'screenshot') and re.fullmatch('[a-z0-9_-]{1,80}', arguments[1]):
        return board(arguments)
    fail('FRDM_DEMO.USAGE', 'build | start 0|180 [--study-config FILE] [--voice-lab-config FILE] | status | presence 0|1 | scenario NAME | screenshot NAME | backlight-samples N | restart-ui | stop [--execute]')

print(json.dumps(main()))
PY
