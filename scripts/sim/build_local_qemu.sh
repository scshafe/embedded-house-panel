#!/bin/sh
# Build pinned QEMU inside .build/qemu; dry-run JSON is the default.
# --execute downloads verified sources and installs only to the repo-local prefix.
# Requires existing Xcode CLT, Python3, pkg-config, GLib, Pixman, libpng, Ninja,
# GnuPG, and Git. Never installs Homebrew packages or accepts a vendor license.
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_directory/../.." && pwd)
case "${1:-}" in ''|--execute) ;; --help) printf '%s\n' 'Usage: build_local_qemu.sh [--execute]'; exit 0 ;; *) exit 2 ;; esac
exec python3 - "$project_root" "${1:-}" <<'PY'
import bz2
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

root = Path(sys.argv[1])
work = root / '.build/qemu'
source = work / 'qemu-11.1.1'
archive = work / 'qemu-11.1.1.tar.xz'
prefix = work / 'install'
venv = work / 'venv'
build = work / 'build'
bundle = prefix/'HousePanelQEMU.app'
sha256 = '079ffbff8a7111bbc89022107cbabf3bbfd614d5fc9d7cc675991196aca12482'
fingerprint = 'CEACC9E15534EBABB82D3FA03353C9CEF108B584'
configure = [str(source / 'configure'), '--prefix=' + str(prefix), '--python=' + str(venv / 'bin/python3'),
             '--target-list=aarch64-softmmu', '--without-default-features', '--enable-system',
             '--enable-tools', '--enable-hvf', '--enable-tcg', '--enable-cocoa', '--enable-slirp',
             '--enable-pixman', '--enable-png', '--disable-docs', '--disable-werror']
commands = [
 ['curl', '-fL', 'https://download.qemu.org/qemu-11.1.1.tar.xz', '-o', str(archive)],
 ['curl', '-fL', 'https://download.qemu.org/qemu-11.1.1.tar.xz.sig', '-o', str(archive) + '.sig'],
 ['curl', '-fL', 'https://keys.openpgp.org/vks/v1/by-fingerprint/' + fingerprint, '-o', str(work/'release-key.asc')],
 ['gpg', '--homedir', str(work/'gnupg'), '--batch', '--yes', '--dearmor', '--output', str(work/'release-key.gpg'), str(work/'release-key.asc')],
 ['gpgv', '--homedir', str(work/'gnupg'), '--keyring', str(work/'release-key.gpg'), str(archive)+'.sig', str(archive)],
 ['tar', '-xJf', str(archive), '-C', str(work)],
 [sys.executable, '-m', 'venv', str(venv)],
 [str(venv/'bin/pip'), 'install', 'meson==1.11.2', 'pycotap==1.3.1', 'tomli==2.4.1'],
 configure, ['ninja', '-j6', 'qemu-system-aarch64', 'qemu-img']]
install_commands = [
 ['install_name_tool', '-rpath', '@loader_path/subprojects/slirp', '@loader_path/../lib', str(prefix/'bin/qemu-system-aarch64')],
 ['codesign', '--entitlements', str(source/'accel/hvf/entitlements.plist'), '--force', '-s', '-', str(prefix/'bin/qemu-system-aarch64')],
 [str(prefix/'bin/qemu-system-aarch64'), '--version'],
 [str(prefix/'bin/qemu-img'), '--version'],
 [str(prefix/'bin/qemu-system-aarch64'), '-accel', 'help']]
print(json.dumps({'execute': sys.argv[2]=='--execute', 'commands': commands, 'configure_and_ninja_working_directory': str(build),
 'archive_sha256': sha256, 'release_key_fingerprint': fingerprint,
 'provenance': 'https://www.qemu.org/download/',
 'libslirp_commit': '26be815b86e8d49add8c9a8b320239b9594ff03d',
 'dependency_policy': 'QEMU release-pinned Meson wraps; existing host GLib/Pixman/libpng; repo-local Python venv',
 'install_prefix': str(prefix),
 'targeted_install': {'copy': ['qemu-system-aarch64', 'qemu-img', 'subprojects/slirp/libslirp.0.dylib'],
                      'firmware_decompress': ['edk2-aarch64-code.fd.bz2', 'edk2-arm-vars.fd.bz2'],
                      'firmware_copy': ['pc-bios/*.rom', 'pc-bios/*.bin'],
                      'commands': install_commands},
 'macos_app_bundle': {'path': str(bundle), 'identifier': 'org.housepanel.qemu-simulation',
                      'display_name': 'QEMU Panel Simulation', 'scope': 'repo-local only'},
 'simulation_not_hardware_evidence': True}), flush=True)
if sys.argv[2] != '--execute':
    sys.exit(0)
if sys.platform != 'darwin':
    raise SystemExit('LOCAL_QEMU.MACOS_REQUIRED')
for executable in ['curl', 'gpg', 'gpgv', 'tar', 'ninja', 'pkg-config', 'git', 'clang', 'install_name_tool', 'codesign']:
    if not shutil.which(executable):
        raise SystemExit('LOCAL_QEMU.HOST_DEPENDENCY_MISSING:' + executable)
for package in ['glib-2.0', 'pixman-1', 'libpng']:
    subprocess.run(['pkg-config', '--exists', package], check=True)
work.mkdir(parents=True, exist_ok=True)
(work/'gnupg').mkdir(mode=0o700, exist_ok=True)
build.mkdir(exist_ok=True)
for index, command in enumerate(commands):
    if index == 0 and archive.exists() and hashlib.sha256(archive.read_bytes()).hexdigest() == sha256:
        continue
    if index == 5:
        if hashlib.sha256(archive.read_bytes()).hexdigest() != sha256:
            raise SystemExit('LOCAL_QEMU.ARCHIVE_DIGEST_MISMATCH')
        if source.exists():
            continue
    subprocess.run(command, cwd=build if index >= 8 else work, check=True)
# Install only the requested binaries and their runtime dependency. A generic
# `ninja install` builds unrelated targets and requires generated docs/traces.
slirp_source = source/'subprojects/slirp'
slirp_commit = subprocess.check_output(['git', '-C', str(slirp_source), 'rev-parse', 'HEAD'], text=True).strip()
if slirp_commit != '26be815b86e8d49add8c9a8b320239b9594ff03d':
    raise SystemExit('LOCAL_QEMU.LIBSLIRP_REVISION_MISMATCH')
for directory in ['bin', 'lib', 'share/qemu']:
    (prefix/directory).mkdir(parents=True, exist_ok=True)
for binary in ['qemu-system-aarch64', 'qemu-img']:
    shutil.copy2(build/binary, prefix/'bin'/binary)
shutil.copy2(build/'subprojects/slirp/libslirp.0.dylib', prefix/'lib/libslirp.0.dylib')
firmware = source/'pc-bios'
for compressed in ['edk2-aarch64-code.fd.bz2', 'edk2-arm-vars.fd.bz2']:
    (prefix/'share/qemu'/compressed.removesuffix('.bz2')).write_bytes(bz2.decompress((firmware/compressed).read_bytes()))
for pattern in ['*.rom', '*.bin']:
    for path in firmware.glob(pattern):
        shutil.copy2(path, prefix/'share/qemu'/path.name)
for command in install_commands:
    subprocess.run(command, check=True)
# A local bundle makes the Cocoa window selectable by accessibility clients.
# No LaunchServices registration or system-wide installation is performed.
for directory in ['Contents/MacOS', 'Contents/lib']:
    (bundle/directory).mkdir(parents=True, exist_ok=True)
shutil.copy2(prefix/'bin/qemu-system-aarch64', bundle/'Contents/MacOS/qemu-system-aarch64')
shutil.copy2(prefix/'lib/libslirp.0.dylib', bundle/'Contents/lib/libslirp.0.dylib')
with (bundle/'Contents/Info.plist').open('wb') as file:
    plistlib.dump({'CFBundleIdentifier': 'org.housepanel.qemu-simulation',
                  'CFBundleName': 'QEMU Panel Simulation',
                  'CFBundleDisplayName': 'QEMU Panel Simulation',
                  'CFBundleExecutable': 'qemu-system-aarch64',
                  'CFBundlePackageType': 'APPL', 'CFBundleVersion': '11.1.1',
                  'CFBundleShortVersionString': '11.1.1', 'LSMinimumSystemVersion': '13.0',
                  'NSHighResolutionCapable': True, 'LSUIElement': False}, file)
subprocess.run(['codesign', '--entitlements', str(source/'accel/hvf/entitlements.plist'),
                '--force', '--deep', '-s', '-', str(bundle)], check=True)
print(json.dumps({'status':'built', 'binary':str(prefix/'bin/qemu-system-aarch64'), 'simulation_not_hardware_evidence':True}))
PY
