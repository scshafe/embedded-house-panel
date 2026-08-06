# Debian Tailnet Bring-up

<!-- @canonical debian-tailnet-bringup -->

- **Compatibility snapshot:** 2026-08-05
- **Status:** Canonical Tailnet procedure after the hardware baseline has
  preserved eMMC. It is not an eMMC-imaging or Buildroot-production procedure.
- **Search terms:** BeagleBone Black, Debian 13.6, Trixie, AM335x, armhf,
  Tailscale, tailnet, `/dev/net/tun`, Lexinet.

This runbook establishes an evidence-backed Tailscale path from a disposable
BeagleBone Black Debian system to one restricted **tailnet** destination. It
starts only after the hardware baseline has preserved the original eMMC and
records the evidence needed before the same capability is ported into the
project-owned Buildroot image.

## Compatibility finding

As of the snapshot date, this is a supported characterization combination:

- BeagleBoard publishes BeagleBone Black Debian 13.6 images for the AM335x
  `armhf` architecture. The project characterization target is the official
  [Debian 13.6 base image with Linux 6.12](https://www.beagleboard.org/distros/beaglebone-black-debian-13-6-2026-07-24-iot-v6-12-x).
  Its official image-builder configuration identifies the release as Debian
  13.6, codename Trixie, architecture `armhf`
  ([configuration source](https://github.com/beagleboard/image-builder/blob/master/configs/bb.org-debian-trixie-base-v6.12-armhf-am335x.conf)).
- Tailscale publishes a Trixie apt repository and `armhf` packages
  ([Trixie repository definition](https://pkgs.tailscale.com/stable/debian/trixie.tailscale-keyring.list),
  [`armhf` package index](https://pkgs.tailscale.com/stable/debian/dists/trixie/main/binary-armhf/Packages)).
  Tailscale's [Linux installation documentation](https://tailscale.com/docs/install/linux)
  includes Debian-based distributions and requires TUN for normal Linux
  operation.
- Tailscale is **not assumed to be bundled** in the BeagleBoard image. The
  official image description does not promise it, and the official image-builder
  source contained no Tailscale integration when checked on the snapshot date.
  Verify its absence or presence on the actual image; install it explicitly when
  absent.
- The standard BeagleBone Black provides 10/100 Ethernet but no onboard Wi-Fi
  ([board features](https://docs.beagleboard.org/boards/beaglebone/black/ch04.html)).
  BeagleBoard's own Wi-Fi instructions require a USB Wi-Fi adapter for this board
  ([Wi-Fi guidance](https://docs.beagleboard.org/books/beaglebone-cookbook/05tips/tips.html)).
  Use wired Ethernet for this baseline. Do not add a USB adapter, its driver, or
  its power demand to the first experiment; the USB host is limited to 500 mA
  under the documented power conditions
  ([power documentation](https://docs.beagleboard.org/boards/beaglebone/black/ch05.html)).

This finding establishes package and architecture availability. It does not
establish adequate battery life, suspend/resume behavior, Lexinet application
compatibility, or production-image suitability.

## System boundary

The current split is intentional:

- **Official Debian 13.6/Linux 6.12 on a fresh microSD** is the disposable
  hardware-characterization environment. Package installation is allowed here
  only after the original eMMC is preserved.
- **Buildroot 2026.05.1/Linux 6.18.21** is the project-owned appliance-image
  candidate. Its kernel configuration, TUN device, Tailscale package, persistent
  state, and resume behavior require separate validation. See the
  [system-image baseline](../../system_image/README.md) and
  [Buildroot decision](../decisions/0002-buildroot-baseline.md).
- **Tailscale** provides transport and node identity. It does not authorize
  Lexinet operations. Lexinet must authenticate and authorize the panel
  independently; that contract remains provisional. See
  [process interconnectivity](../architecture/process-interconnectivity.md),
  the [Lexinet contract](../interfaces/lexinet-contract.md), and the
  [threat model](../security/threat-model.md).

Tailscale grants and shields-up mode constrain tailnet traffic only. They do
not restrict ordinary LAN or internet traffic through `eth0`. This Debian
experiment therefore runs on a controlled lab network without a Lexinet
application credential unless a separate, reviewed host/process egress control
is active. Product acceptance requires evidence for that non-tailnet egress
boundary; a narrow Tailscale grant cannot supply it.

A successful Debian test is a compatibility data point, not proof that the
Buildroot image works.

## Non-negotiable safety and secret rules

1. Do not begin this runbook until Step 2 of the
   [hardware baseline plan](../../.claude/plans/2026-08-05-beaglebone-hardware-baseline.md)
   has inventoried, imaged, and hashed the installed eMMC. That plan currently
   records the required evidence gate, not an executable block-device imaging
   procedure. Stop until the observed board identity supports a separately
   reviewed preservation procedure; do not improvise destructive device names.
2. Use a newly written **non-flasher** microSD image. Confirm that the root
   filesystem is on the microSD and that eMMC is unmounted or read-only before
   installing anything.
3. Never put Tailscale auth keys, browser login URLs, node state, cookies,
   Lexinet credentials, or other secrets in Git, command arguments, URLs,
   terminal capture, logs, image overlays, diagnostics, or issue text.
4. Do not use `curl | sh` in durable automation. Download repository material to
   named files, validate the expected repository definition, and install it in
   explicit steps.
5. Do not silently fall back to Tailscale userspace networking. It exposes proxy
   endpoints rather than the normal transparent TUN path
   ([userspace networking](https://tailscale.com/docs/concepts/userspace-networking)).
   Treat it as a separately approved experiment if kernel TUN cannot be made to
   work.
6. Tailnet policy is not a host firewall. Keep the Debian baseline on a
   controlled wired network and do not describe its ordinary Ethernet egress as
   restricted.

## 1. Prepare and identify the disposable Debian system

Download the official Linux 6.12 image from the BeagleBoard image page above and
verify its published checksum before writing it to the microSD. Record the image
URL, exact filename, publication date, and checksum. Do not select an image whose
name or documentation says it flashes eMMC.

Before first boot, use BeagleBoard's `bb-imager` first-boot settings or the
image's documented `sysconf.txt` mechanism to assign a unique, temporary
operator password. Never reuse or record that password. Treat the microSD and
its FAT partition as secret-bearing media. Prefer a local serial console; if
using SSH, connect only through the controlled wired lab network after resolving
the DHCP lease out of band. Cockpit and SSH are characterization surfaces, not
services to expose through the tailnet.

After the eMMC-preservation gate has passed, boot with wired Ethernet attached
and collect:

```bash
cat /etc/os-release
cat /etc/debian_version
uname -m
dpkg --print-architecture
uname -r
findmnt /
lsblk -o NAME,TYPE,FSTYPE,SIZE,RO,MOUNTPOINTS
ip -brief link
ip -brief address
```

Expected results for this runbook are Debian 13/Trixie in `/etc/os-release`,
point release 13.6 in `/etc/debian_version`, Debian architecture `armhf`, a 6.12
kernel, a root filesystem on the microSD, and no writable eMMC mount. The
verified image filename and checksum remain the authoritative image-revision
identity. `uname -m` may use a kernel architecture spelling instead of `armhf`;
the package-architecture assertion comes from `dpkg --print-architecture`.

Stop if the root device or eMMC state is ambiguous. Do not “fix” an unexpected
mount in place; capture the observation and return to the baseline plan.

## 2. Establish the normal TUN path

Check the character device before installing Tailscale:

```bash
test -c /dev/net/tun
sudo modprobe tun
test -c /dev/net/tun
```

The first check may succeed when TUN is built in or already loaded. Record:

- whether `/dev/net/tun` existed before `modprobe`;
- whether `modprobe tun` succeeded;
- whether the final check succeeded;
- the exact error and relevant kernel log excerpt if it failed.

Do not continue with a userspace-mode workaround under this runbook. A missing
TUN device is evidence against the intended production path.

## 3. Install Tailscale from the official Trixie repository

First establish whether the image already contains Tailscale:

```bash
command -v tailscale
```

If that command succeeds, record the origin and version before deciding whether
to replace it. If it fails, perform the official repository installation as
explicit steps:

```bash
tailscale_repo_work=$(mktemp -d)

curl --fail --show-error --location \
  --output "$tailscale_repo_work/tailscale-archive-keyring.gpg" \
  https://pkgs.tailscale.com/stable/debian/trixie.noarmor.gpg

curl --fail --show-error --location \
  --output "$tailscale_repo_work/tailscale.list" \
  https://pkgs.tailscale.com/stable/debian/trixie.tailscale-keyring.list

grep -Fqx \
  'deb [signed-by=/usr/share/keyrings/tailscale-archive-keyring.gpg] https://pkgs.tailscale.com/stable/debian trixie main' \
  "$tailscale_repo_work/tailscale.list"

sha256sum \
  "$tailscale_repo_work/tailscale-archive-keyring.gpg" \
  "$tailscale_repo_work/tailscale.list"

sudo install -d -m 0755 /usr/share/keyrings
sudo install -m 0644 \
  "$tailscale_repo_work/tailscale-archive-keyring.gpg" \
  /usr/share/keyrings/tailscale-archive-keyring.gpg
sudo install -m 0644 \
  "$tailscale_repo_work/tailscale.list" \
  /etc/apt/sources.list.d/tailscale.list

sudo apt-get update
apt-cache policy tailscale
sudo apt-get install tailscale
```

The exact `grep` is a fail-closed check that the downloaded source definition
matches the repository expected by this timestamped runbook. Record the two
public-file hashes and the package candidate reported by `apt-cache policy` so
another trial can identify drift. The repository can publish a newer candidate;
this characterization procedure records the resolved version rather than
pretending it is the production pin. Stop and inspect an upstream change if the
definition check fails; do not weaken it during an unattended run.

Validate the installed surface:

```bash
command -v tailscale
tailscale version
systemctl is-enabled tailscaled
systemctl is-active tailscaled
test -c /dev/net/tun
```

Record the package version and service state. Do not record the contents of
`/var/lib/tailscale`.

## 4. Define least-privilege tailnet policy before joining

Create a dedicated service tag such as `tag:embedded-house-panel`. Per
Tailscale's [tag documentation](https://tailscale.com/docs/features/tags), the
tag must have an owner in the tailnet policy before the node advertises it.

Grant that tag access only to the exact Lexinet tailnet destination and port.
Tailscale [grants](https://tailscale.com/docs/reference/syntax/grants) are the
current policy syntax. The shape below is illustrative HuJSON, not a value to
copy without substitution:

```json
"tagOwners": {
  "tag:embedded-house-panel": ["autogroup:admin"],
},
"grants": [
  {
    "src": ["tag:embedded-house-panel"],
    "dst": ["lexinet"],
    "ip": ["tcp:443"],
  },
],
```

Replace `lexinet` with the explicit host alias selected in the tailnet policy and
replace `443` with the actual Lexinet service port. Account separately for the
minimum DNS and time services the panel needs. Before applying the rule, review
the complete policy for an existing allow-all ACL or broader grant; a narrow new
rule does not cancel broad access already allowed. Tailscale documents the
default and policy interaction in its [access-control guidance](https://tailscale.com/docs/features/access-control/acls).

Record a sanitized policy revision and rule shape. Do not put private tailnet
hostnames, addresses, or unrelated policy in committed evidence.

## 5. Join interactively for characterization

Before joining, inventory every listening TCP and UDP socket:

```bash
sudo ss -lntup
```

The current official image can include SSH and Cockpit. Preserve a redacted
listener summary for the inbound negative control; do not commit addresses or
an unredacted process inventory.

For this disposable characterization system only, pause all terminal recording,
then join with browser-mediated interactive authentication and inbound tailnet
connections blocked:

```bash
sudo tailscale up \
  --advertise-tags=tag:embedded-house-panel \
  --shields-up
```

Do not add `--auth-key`. Do not capture, paste into notes, or commit the browser
login URL printed by the command. Complete authentication through the trusted
operator browser. Resume terminal recording only after the login URL is absent
from the captured session and scrollback. Verify in the Tailscale admin console
that the node carries the intended tag. Tailscale documents `--shields-up` as
blocking incoming tailnet connections in the
[`tailscale up` reference](https://tailscale.com/docs/reference/tailscale-cli/up).

Tagged nodes have key expiry disabled by default. Record the expiry state and
enable expiry for this disposable node unless a time-bounded characterization
run has a documented reason not to. Expiry does not replace explicit node
removal during retirement.

This interactive step is not the eventual manufacturing or Buildroot
provisioning mechanism. It avoids designing a persistent secret flow before the
hardware path has been validated.

## 6. Validate both allow and deny behavior

Validate each boundary independently:

1. Confirm `tailscaled` is active and the node appears in the tailnet with the
   dedicated tag, expected expiry posture, and shields-up mode.
2. Confirm a tailnet path to the exact Lexinet destination.
3. Make an HTTPS request through the real Lexinet client path and verify TLS
   normally. A successful Tailscale peer check is not a successful Lexinet
   request.
4. Do not supply a Lexinet application credential unless both its approved
   secret mechanism and a separately reviewed host/process egress control are
   active. Otherwise record application authorization as **not yet validated**.
5. Use a known-live canary destination for the outbound policy negative control.
   Immediately before and after the panel trial, prove the canary listener is
   reachable from a separately authorized control node. The panel must be denied
   while that control succeeds. A closed port, offline peer, or generic timeout
   is not evidence that policy denied the connection.
6. From a separately authorized tailnet peer, attempt an inbound connection to a
   non-loopback listener established by the socket inventory. The same listener
   must be reachable through the controlled LAN and blocked through the panel's
   tailnet address. If there is no safe known-live listener, record shields-up
   enforcement as **not yet validated** instead of accepting an ambiguous
   failure.
7. Record explicitly that these controls do not constrain ordinary `eth0`
   egress. Do not claim the product's principal-specific egress gate has passed.

Tailscale answers “may this tagged node reach this tailnet destination?” Lexinet
must still answer “may this application principal perform this operation?” A
host/process boundary must separately govern non-tailnet egress. The three
controls are not substitutes.

## 7. Characterize DeepSleep0 recovery

The current power analysis expects DeepSleep0 to remove Ethernet and tailnet
availability. See [sleep/wake strategies](../research/sleep-wake-strategies.md).
Tailscale recovery after resume is asynchronous; the display and UI must not
wait synchronously for it.

For each suspend/resume trial, record separate timestamps for:

1. Linux resume observed;
2. wired carrier restored;
3. DHCP address and DNS restored;
4. `tailscaled` active;
5. intended tailnet peer reachable;
6. Lexinet HTTPS reachable;
7. Lexinet application authorization ready.

Do not run `tailscale up` on every resume. Existing node state on the disposable
microSD should reconnect. A failure to reconnect, excessive retry traffic, or a
long recovery tail is hardware/product evidence, not an invitation to hide the
state behind a blocking UI retry loop.

## Evidence record

Follow the storage and redaction rules in the
[hardware test guide](../../hardware_tests/README.md). Keep raw results under the
ignored `hardware_tests/results/` tree. Promote only a curated, redacted summary
after review.

Record at minimum:

- UTC start/end timestamps, board model/revision, power source, and network
  topology;
- exact image URL, filename, publication date, and verified checksum;
- OS release, Debian architecture, kernel version, root device, and eMMC
  preservation/mount state;
- Ethernet interface and driver, with MAC addresses and assigned addresses
  redacted from committed summaries;
- TUN device state, module state, and exact failure evidence;
- apt repository URL and validated repository-line shape;
- public repository-file hashes, resolved Tailscale package version, and
  `tailscaled` service state;
- redacted pre-join listener inventory;
- service tag, expiry state, shields-up setting, and sanitized policy
  revision/rule shape;
- permitted destination, known-live denied outbound canary, and known-live
  denied inbound control results;
- an explicit statement that ordinary Ethernet egress was not constrained;
- suspend state requested versus state observed;
- each resume milestone above, with elapsed times, retries, and failures.

Never commit:

- an auth key, browser login URL, cookie, token, or Lexinet credential;
- `/var/lib/tailscale` or an image containing its live node state;
- raw `tailscale status --json` output, which can expose tailnet topology;
- private tailnet IP addresses, DNS names, host aliases, MAC addresses, or
  unrelated peers;
- unredacted command transcripts or diagnostics.

## Rollback, reimage, and revocation

Before discarding, reusing, or reimaging the characterization microSD:

1. Preserve the redacted evidence needed for the hardware baseline.
2. Remove the device in the Tailscale admin console. Tailscale states that admin
   removal immediately revokes tailnet access; uninstalling the package alone
   does not remove the device
   ([remove a device](https://tailscale.com/kb/1260/device-remove),
   [node-key lifecycle](https://tailscale.com/docs/concepts/node-keys)).
3. Revoke the Lexinet application credential independently if one was issued.
4. Reimage or securely retire the disposable microSD. Do not copy its
   `/var/lib/tailscale` state to another image.

For a lost or potentially compromised board, perform steps 2 and 3 immediately.
Do not wait to recover the physical media. The preserved eMMC remains outside
this rollback procedure and must stay untouched.

## Buildroot handoff gate

Do not translate this apt procedure into the Buildroot overlay. The current
Buildroot baseline packages Tailscale **1.78.1**, which the repository already
marks as sufficient for image integration only and stale for a networked product.
Before production use:

- select and pin a reviewed Tailscale release supported by the chosen Buildroot
  toolchain and `armhf` target;
- validate `/dev/net/tun` on the Buildroot Linux 6.18.21 kernel;
- define a provisioning path that does not expose reusable keys in image
  overlays, command arguments, URLs, logs, or diagnostics;
- define ownership, permissions, durability, reset, and revocation for node
  state without baking live state into the image;
- implement and test the intended principal-specific non-tailnet egress boundary
  rather than treating Tailscale grants as a host firewall;
- default to no inbound tailnet services and validate any deliberate exception;
- repeat the positive destination, negative policy, TLS, Lexinet authorization,
  DeepSleep0, and asynchronous reconnect tests on Buildroot;
- keep wired Ethernet as the baseline until a Wi-Fi adapter, driver, antenna,
  and battery-power budget are chosen and measured.

The Debian Tailnet gate passes when the normal TUN path, least-privilege tailnet
rule, positive Lexinet transport check, known-live outbound and inbound negative
controls, and suspend/resume reconnect are evidenced without exposing secrets.
The principal-specific non-tailnet egress gate and the Buildroot gate remain
separate and closed until their controls are implemented and evidenced on the
production image.
