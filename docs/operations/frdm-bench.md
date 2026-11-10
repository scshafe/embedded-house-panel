<!-- @canonical frdm-bench-operations -->

# The Bench Tier: the Panel on the Vendor Image, Every Day

**Status (2026-11-10):** built. Not installed on the board yet.
[ADR 0014](../decisions/0014-run-a-bench-tier-on-the-vendor-image.md)
accepts this tier. Nothing it records is gate evidence: every record says
`bench_not_gate_evidence`, and it runs the vendor's 6.18 kernel.

## What it is

The project's services run from `/opt/house-panel` on NXP's vendor image.
systemd starts them at boot, with weston off. The owner gets:
- the clock in the configured time zone;
- day and night from the configured location;
- the vocabulary cards;
- the Report button;
- music, once the Spotify sign-in is done.

| Service | Runs as | Needs |
|---|---|---|
| `bhp-bench-prepare` (once per start) | root | `/etc/house-panel/bench.json` |
| `bhp-bench-pir`: the simulated PIR device | root | |
| `bhp-bench-presence`: the awake hours | root | |
| `bhp-bench-supervisor` | root (owns the backlight) | |
| `bhp-bench-broker`: the microphone array | 1103 | |
| `bhp-bench-reporter`: bug reports | 1106 | `reporter.json`, then the tailnet |
| `bhp-bench-music` | 1105 | `music.json` and the Spotify sign-in |
| `bhp-bench-uploader`: voice-lab | 1107 | `voice-lab.json`, then the tailnet |
| `bhp-bench-ui` | 1102 | |

**Restarts.** Every service restarts on exit, waiting from 2 s up to 2 min.
With a reporter, an exit becomes a restart report to the inbox.

**Presence.** There is no PIR.
- The panel counts as present during the awake hours (06:30 to 23:00 by
  default).
- At any other time, a touch lights it for `touch_presence_seconds` (300 by
  default).
- When nobody is present, the backlight goes off. Suspend stays disabled.

**Day and night.** These come from the supervisor's solar calculation at the
configured location, so the panel turns red after sunset.

## What it writes on the board

Exactly what ADR 0014 allows:
- **`/opt/house-panel/`:**
  - `releases/<id>/` and the `current` and `previous` links;
  - `install.json`, which records weston's state before the first install;
  - `state/`: each service's directory, owned by its UID.
- **`/etc/house-panel/`:** the configuration. You write it, or the agent
  writes it from values you give.
- **The units:** `/etc/systemd/system/bhp-bench-*.service` and
  `bhp-bench.target`. weston's units are disabled and masked.
- **Runtime directories:** `/run/house-panel` and `/run/bhp-sim`, which
  vanish at power-off.

**Never:**
- a package;
- a user database entry (services use bare UIDs through `setpriv`);
- `/etc/localtime`, sshd, `authorized_keys`, U-Boot or the boot partition.

## Owner steps

### 1. Close the root console (before any token goes on the board)

On the vendor image root has an empty password, so the serial console and
`tty1` give a root shell (2026-11-10,
`hardware_tests/results/frdm/bench-survey-20261110/`). sshd already takes
keys only.

Set a root password yourself, so that no agent ever sees it. On the serial
console, or over ssh with your own key, run `passwd`:

```sh
ssh -t root@192.168.50.19 passwd
```

Keep the password with your other device credentials; it is the console's
only way in now. Then tell the agent "console closed". It checks, read-only,
that root's password field is no longer empty, and it never reads the field
itself.

### 2. The bench configuration

`/etc/house-panel/bench.json`:
- **`latitude` and `longitude`:** two decimals are enough, about 1 km.
- **`timezone`:** an IANA name.
- **`rotation`:** `"180"`, as mounted.
- **`awake`:** optional, the hours the panel counts as present.
- **`touch_presence_seconds`:** optional, 1 to 3600.

Either give the agent the five values, or run this from the PC with yours:

```sh
ssh root@192.168.50.19 'install -d -m 755 /etc/house-panel && cat > /etc/house-panel/bench.json' <<'EOF'
{"latitude": 00.00, "longitude": 00.00, "timezone": "America/Chicago", "rotation": "180",
 "awake": {"from": "06:30", "to": "23:00"}, "touch_presence_seconds": 300}
EOF
```

The file names the house's location, so it never enters Git. Until it
exists, `prepare` refuses with `BENCH.CONFIG_MISSING`, and nothing else
starts.

### 3. The tailnet (approved; the key is yours)

Enrolment needs `tailscale` on the board, and the vendor image has none.
ADR 0014 allows only the project's own files. Placing the official static
Tailscale `1.102.2` (ADR 0010's pin and hash) in `/opt/house-panel` is
therefore your decision.

Once that is settled, you mint the auth key with the panel's one tag and
enter it on the board yourself. The agent never handles it. After that,
`reporter.json` (`{"inbox_url": "https://<PC>:8743"}`), the live vocabulary
study and voice-lab can be turned on.

### 4. Optional services

- **Music:** follow [spotify-owner-setup.md](spotify-owner-setup.md),
  Parts 1–3 (after step 1). Then the agent writes `music.json`.
- **Voice-lab:** `voice-lab.json`, as for the demo (no credential), and the
  sign-in of issue #23.
- **Live vocabulary:** `vocabulary-panel.env` (no credential; ADR 0010).

## Commands (from the PC)

```sh
set -a; . ./config/device.env; set +a
scripts/deploy/frdm_bench.sh build --execute     # cross-builds .build/frdm-bench-bundle.tar.gz
scripts/deploy/frdm_bench.sh install --execute   # verify, unpack, swap, (re)start
scripts/deploy/frdm_bench.sh status --execute
scripts/deploy/frdm_bench.sh logs ui --execute   # or prepare, supervisor, broker, reporter, music, ...
scripts/deploy/frdm_bench.sh rollback --execute  # back to the previous release
scripts/deploy/frdm_bench.sh uninstall --execute # units and releases go, state stays, weston returns
scripts/deploy/frdm_bench.sh uninstall --purge --execute  # and /opt/house-panel/state too
```

Without `--execute`, each command prints its plan. `build` needs the vendor
sysroot (`scripts/build/frdm_vendor_sysroot.sh --execute`).

Each board command keeps its record under
`hardware_tests/results/frdm/bench-<UTC>/`.

### What `install` checks

1. The tarball's SHA-256, computed on the PC.
2. Every file against `SHA256SUMS`: no extra file, no link, no path outside
   the release.
3. That the installer in the bundle is the one running.
4. That each binary is aarch64 and its libraries resolve with `ldd`.

Only then does it swap `current`, keeping the release before it as
`previous`.

### After changing `/etc/house-panel`

Run `ssh root@192.168.50.19 systemctl restart bhp-bench.target`. That runs
`prepare` again.

## Undoing it

- **`uninstall`** stops and removes the bench and gives weston back as it
  was. `/etc/house-panel` stays.
- **`--purge`** also removes the tokens and queues under `state/`.
- **The factory backup** (`docs/operations/frdm-bringup.md`) restores the
  whole eMMC.
