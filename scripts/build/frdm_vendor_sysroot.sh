#!/bin/sh
# Builds the aarch64 sysroot for cross builds that run on the FRDM vendor image.
# Inputs: [--execute]. Default is a JSON dry run listing every download.
# Side effects with --execute: downloads pinned Debian trixie arm64 .debs from
#   snapshot.debian.org into .build/frdm-sysroot-debs/, checks each SHA-256, and
#   unpacks them into .build/frdm-sysroot/. The board image supplies the same
#   sonames at run time: libc 6, libdrm 2, libpng16 16, libz 1, libcrypto 3. Needs no root; installs nothing.
# Idempotent: a verified .deb is not downloaded again; the sysroot is rebuilt.
# Trixie's glibc 2.41 is not newer than the vendor image's 2.43 (2026-10-18).
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_directory/../.." && pwd)
execute=${1:-}
case "$execute" in ''|--execute) ;; *) printf '%s\n' "Usage: $0 [--execute]" >&2; exit 2 ;; esac
exec python3 -I - "$project_root" "$execute" <<'PY'
import hashlib, json, shutil, subprocess, sys, urllib.request
from pathlib import Path
root = Path(sys.argv[1]); execute = sys.argv[2] == '--execute'
SNAPSHOT = 'https://snapshot.debian.org/archive/debian/20261015T000000Z/'
# Debian 13.7 (trixie) main/binary-arm64, Packages.xz SHA-256
# be16b37b2f740f066ea93d35d56f04f94d63693e5a3eb421b052bf86fae75bcd.
PACKAGES = [
    ('pool/main/g/glibc/libc6_2.41-12+deb13u4_arm64.deb', '8784eda966b189c777a384dac5ce009e8fc9b52d006926c5a013e7fa8aa688cc'),
    ('pool/main/g/glibc/libc6-dev_2.41-12+deb13u4_arm64.deb', '71e373cb5e9da975b02fb4af555929cb1977560e2dd8245de4eb8a7b03677a08'),
    ('pool/main/l/linux/linux-libc-dev_6.12.107-1_all.deb', 'dc68dd53c7356598d005866da0d19d7bd33af2a62971aced624cdf04492902b4'),
    ('pool/main/libx/libxcrypt/libcrypt1_4.4.38-1_arm64.deb', 'b9aaa808aa3812b3de3e54eb5dfb16762c1779db032604abef2354c02fdc29b8'),
    ('pool/main/libx/libxcrypt/libcrypt-dev_4.4.38-1_arm64.deb', 'eec6d4658495cdb8ce505cad91e0918edb8086677f280f3851c07ccd70543054'),
    ('pool/main/g/gcc-14/gcc-14-base_14.2.0-19_arm64.deb', '34ee90679b018c0e64234747a4c4c0ae6b7f63541115037465a8627c2dfbc594'),
    ('pool/main/g/gcc-14/libgcc-s1_14.2.0-19_arm64.deb', '1108bc87879833d6d9a145f22a4a15cddb34e065b4b5f4b97bee586adbac2851'),
    ('pool/main/g/gcc-14/libgcc-14-dev_14.2.0-19_arm64.deb', '64b8ebc7182a69aa9525df6eab5e7eb849b947e350d821549f8a8a3e9b19e5ba'),
    ('pool/main/g/gcc-14/libatomic1_14.2.0-19_arm64.deb', '7ccff90234484bf21ae5ecb79d25b6a84b2f4038f65dd139085b2e3a937cd8dc'),
    ('pool/main/g/gcc-14/libstdc++6_14.2.0-19_arm64.deb', '6669b0c52a2e7c6af9adfdabce3ff6e286065cdfbc7b85280862b5f799daebee'),
    ('pool/main/g/gcc-14/libstdc++-14-dev_14.2.0-19_arm64.deb', '8760122d044cd8eb8c69426311d6b5ce7bb8f0f76e9cda1944d153ae878138f8'),
    ('pool/main/libd/libdrm/libdrm2_2.4.124-2_arm64.deb', 'b535506630bb6a9a616bc07caf3b29915f980d8c3437852fb96ea64fb1a8a3f8'),
    ('pool/main/libd/libdrm/libdrm-dev_2.4.124-2_arm64.deb', '7871c74757d8d000c900dbbdb0e81a2665893fcab1704b6019953529bb4c0f4a'),
    ('pool/main/libp/libpng1.6/libpng16-16t64_1.6.48-1+deb13u5_arm64.deb', '11096ad43504ca24e8044bcc89e922d932a2812ff25568613b28d22235190ba5'),
    ('pool/main/libp/libpng1.6/libpng-dev_1.6.48-1+deb13u5_arm64.deb', 'a212fde02b2930302edcc959eaa2079bb459def3a92741cc09f6f55c250b8ca0'),
    ('pool/main/z/zlib/zlib1g_1.3.dfsg+really1.3.1-1+b1_arm64.deb', '209aa5cf671e97b9eb0410844fa6df4cae2e75b0c72e7802ab6c8ece13e6ddef'),
    ('pool/main/z/zlib/zlib1g-dev_1.3.dfsg+really1.3.1-1+b1_arm64.deb', '745ebcf1fa115e230af76216355d7290a24f1790fe3780249b519c0e4d30534f'),
    ('pool/main/o/openssl/libssl3t64_3.5.7-1~deb13u2_arm64.deb', 'ec131326aa9fa9ec934eca386bc7991f328fe383eaefd3e43bd8901a9199c5ae'),
    ('pool/main/o/openssl/libssl-dev_3.5.7-1~deb13u2_arm64.deb', 'b81910e8728fab596158c8384d6aa101c8249c1769eb2d33c73643b2be7a0bcf'),
]
debs = root/'.build/frdm-sysroot-debs'; sysroot = root/'.build/frdm-sysroot'
print(json.dumps({'execute': execute, 'snapshot': SNAPSHOT, 'downloads': [p for p, _ in PACKAGES],
                  'sysroot': str(sysroot), 'evidence': 'host_build_input_not_hardware_evidence'}), flush=True)
if not execute:
    sys.exit(0)
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
debs.mkdir(parents=True, exist_ok=True)
for pool_path, expected in PACKAGES:
    target = debs/Path(pool_path).name
    if not target.exists() or digest(target) != expected:
        partial = target.with_suffix('.partial')
        with urllib.request.urlopen(SNAPSHOT + pool_path, timeout=120) as response:
            partial.write_bytes(response.read())
        if digest(partial) != expected:
            partial.unlink()
            raise SystemExit('FRDM_SYSROOT.SHA256_MISMATCH ' + pool_path)
        partial.replace(target)
if sysroot.exists():
    shutil.rmtree(sysroot)
sysroot.mkdir(parents=True)
for pool_path, _ in PACKAGES:
    deb = debs/Path(pool_path).name
    members = subprocess.run(['bsdtar', '-tf', str(deb)], check=True, capture_output=True, text=True).stdout.split()
    data = [m for m in members if m.startswith('data.tar')]
    if len(data) != 1:
        raise SystemExit('FRDM_SYSROOT.DATA_MEMBER_INVALID ' + deb.name)
    payload = subprocess.run(['bsdtar', '-xOf', str(deb), data[0]], check=True, capture_output=True).stdout
    subprocess.run(['bsdtar', '-xf', '-', '-C', str(sysroot)], input=payload, check=True)
# Trixie is merged-/usr: its linker scripts name /lib/..., which is /usr/lib.
for name in ('lib', 'bin', 'sbin'):
    if not (sysroot/name).exists():
        (sysroot/name).symlink_to('usr/'+name)
# Debian ships absolute symlinks (for example libdrm.so -> /usr/lib/...);
# point them inside the sysroot so the linker never reads the host's files.
for link in sysroot.rglob('*'):
    if link.is_symlink():
        target = Path(link.readlink())
        if target.is_absolute():
            link.unlink()
            link.symlink_to(Path(*(['..'] * (len(link.relative_to(sysroot).parts) - 1))) / target.relative_to('/'))
print(json.dumps({'sysroot': str(sysroot), 'packages': len(PACKAGES), 'result': 'ready'}), flush=True)
PY
