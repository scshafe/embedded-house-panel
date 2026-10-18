<!-- @canonical frdm-bringup -->

# FRDM i.MX 8M Plus bring-up

Authority: [ADR 0008](../decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md).
Board facts: [`config/boards/frdm-imx8mp.env`](../../config/boards/frdm-imx8mp.env).
Evidence scripts: [`hardware_tests/frdm/`](../../hardware_tests/README.md#frdm-imx-8m-plus-gate-evidence).
Plan: [`.claude/plans/2026-09-25-frdm-hardware-baseline.md`](../../.claude/plans/2026-09-25-frdm-hardware-baseline.md).

Status: written before the board arrived; ARRIVAL-01 and -03 to -06 have run
since (see [Arrival status](#arrival-status-2026-10-05)). No gate is closed.
Every value marked `UNMEASURED:` in the profile is closed only by a
reviewed commit that links the JSON evidence named in the gate map.

## Order

1. **Vendor baseline.** Boot NXP's stock Yocto image (preinstalled on eMMC, or
   reflashed from the product page). It carries NXP's firmware under NXP's
   terms, so discovery, display, audio, and Wi-Fi evidence can start before the
   project image exists.
2. **Project image.** Build and flash the bring-up image only after two blockers
   clear: `FRDM-GATE-PIR` values (the project DTB refuses to build without them)
   and the `NXP-FIRMWARE-EULA` human decision (post-image refuses without the
   LPDDR4 training firmware).
3. **Wake path.** On the project image, prove gpio-keys PIR events, then
   suspend entry with an attributed PIR wake.

## Access paths

### Serial console

- Connector `J19` (`BHP_BOARD_DEBUG_USB_CONNECTOR`), USB-C to the host. It
  enumerates two serial ports; the first is the Cortex-A53 console
  (`ttymxc1`, 115200 8N1).
- macOS host: `screen <SERIAL_DEVICE> 115200` with `<SERIAL_DEVICE>` from
  `ls /dev/cu.usbserial-*`. Linux host: `picocom -b 115200 <SERIAL_DEVICE>`.
- Keep this console attached for every flash and suspend step.

### Boot switch SW5

| Boot source | SW5[1-4] | Profile key |
|---|---|---|
| eMMC (default) | `0010` | `BHP_BOARD_BOOT_SWITCH_EMMC` |
| microSD | `0011` | `BHP_BOARD_BOOT_SWITCH_MICROSD` |
| USB serial download (uuu) | `0001` | `BHP_BOARD_BOOT_SWITCH_USB_SERIAL_DOWNLOAD` |

Power off before moving SW5. Power is `J2` only: a USB-C PD charger offering
12–20 V at 45 W or more (65 W preferred). The USB download port is `J3`.

### Flashing with uuu

Install NXP's `uuu` on the host. Set SW5 to `0001`, connect `J3` to the host,
power on, and confirm with `uuu -lsusb`.

```sh
# eMMC (the stock image lives here; back it up first, see ARRIVAL-05; restore: frdm-factory-restore.md)
uuu -b emmc_all <BOOTLOADER> <IMAGE>
# microSD in the board's slot, written over USB
uuu -b sd_all <BOOTLOADER> <IMAGE>
```

Vendor image: `<BOOTLOADER>`/`<IMAGE>` are the `imx-boot-*-frdm*` and
`*.wic.zst` files of the downloaded release. Project image:
`$BHP_BUILDROOT_OUTPUT_DIR/images/imx8-boot-sd.bin` and
`$BHP_BUILDROOT_OUTPUT_DIR/images/sdcard.img`. Then power off, set SW5 back to
`0010` (eMMC) or `0011` (microSD), and power on. To write a microSD card on the
host instead, positively identify the card (`diskutil list` / `lsblk`) and use a
separately reviewed `dd`; no repository script guesses a block device.

### SSH bring-up profile

The production image has no SSH server. The bring-up image adds Dropbear with
root public-key login only (see
[board-bringup-provisioning.md](board-bringup-provisioning.md) for the security
boundary):

```sh
export BHP_BOARD=frdm-imx8mp
export BHP_BUILDROOT_DIR=<BUILDROOT_2026_05_1_DIR>       # from download_verified_buildroot.sh
export BHP_BUILDROOT_OUTPUT_DIR=<ABSOLUTE_BRINGUP_OUTPUT_DIR>
export BHP_BRINGUP_AUTHORIZED_KEYS_FILE=<ABSOLUTE_PATH>/id_ed25519.pub
scripts/build/build_bringup_image.sh
```

The image requests DHCP on `eth0` (`BHP_BOARD_BRINGUP_DHCP_INTERFACE`); which
RJ45 jack is `eth0` is part of `FRDM-GATE-WIFI` inventory. Log in with
`ssh -o IdentitiesOnly=yes -i <ABSOLUTE_PATH>/id_ed25519 root@<BOARD_ADDRESS>`.

### Moving evidence scripts and results

Both images have `ssh` and `tar` (the vendor image's SSH server is unverified;
use a USB drive if it has none). All three commands default to `--dry-run`:

```sh
export BHP_BOARD_SSH_TARGET=root@<BOARD_ADDRESS>
scripts/deploy/frdm_evidence_bundle.sh pack
scripts/deploy/frdm_evidence_bundle.sh push --execute    # -> /root/bhp-evidence on the board
scripts/deploy/frdm_evidence_bundle.sh fetch --execute   # -> hardware_tests/results/frdm (never overwrites)
```

On the board, run scripts from `/root/bhp-evidence` as root. Each prints the path
of the JSON record it wrote.

## Gate map

| Gate | Evidence script | Closes profile keys | Also needs |
|---|---|---|---|
| `FRDM-GATE-IDENTITY` | `frdm_board_identity.sh --board-marking '<BOARD_MARKING>'` | none (EXPECTED_* checks) | photo of the board label |
| `FRDM-GATE-PIR` | `frdm_j18_gpio_inventory.sh`, `frdm_pir_edge_capture.sh --mode gpio`, then `--mode evdev` | `PIR_HEADER_PIN`, `HEADER_IO_VOLTAGE_MV`, `PIR_GPIO_CONTROLLER`, `PIR_GPIO_LINE`, `PIR_PAD_MUX`, `PIR_PAD_OWNER_TO_DISABLE`, `PIR_ACTIVE_LEVEL` | schematic J18 table; meter |
| `FRDM-GATE-DISPLAY` | `frdm_display_touch_backlight.sh [--execute-test-pattern 30]` | `DRM_DEVICE`, `DRM_CONNECTOR_ID`, `FBDEV_DEVICE` | operator sees the pattern |
| `FRDM-GATE-TOUCH` | same run | `TOUCH_INPUT_NAME`, `TOUCH_INPUT_PHYS`, `TOUCH_INTERRUPT` | ADR 0005 provenance path later |
| `FRDM-GATE-BACKLIGHT` | same run, `--execute-backlight-off-trial 5` | `BACKLIGHT_SYSFS_DIR`, `BACKLIGHT_OFF_VALUE` | operator sees a dark panel |
| `FRDM-GATE-SUSPEND` | `frdm_suspend_wake.sh --execute --mem-sleep deep --safety-rtc-seconds 120` | `MEM_SLEEP_STATE` | power in `mem` (POWER row) |
| `FRDM-GATE-WIFI` | `frdm_wifi_resume_timing.sh --observe-seconds 120 [--after-next-resume]` | `WIFI_INTERFACE` | repeated resume samples |
| `FRDM-GATE-AUDIO` | `frdm_mic_capture.sh --duration-seconds 5` | `AUDIO_CAPTURE_DEVICE` | CTIA headset in the 3.5 mm jack |
| `FRDM-GATE-POWER` | `frdm_power_input_log.sh --software-state <STATE> ... --meter-csv <METER_CSV>` | none (power records) | inline USB-C PD meter at `J2` |

A reviewer copies `candidates[].candidate_value` into the profile, runs
`make verify`, and commits with the evidence paths in the message.
`scripts/boards/show_board_profile.sh` lists every key that is still open.

## Board-arrival checklist

Each step: **run** (exact command; `<...>` is a placeholder the user fills),
**pass when** (machine-checkable), **records**. Stop at the first failed step.
Steps marked `HUMAN` need a person at the board.

| # | Where | Run | Pass when | Records |
|---|---|---|---|---|
| ARRIVAL-01 | host | `git switch pre-board && make verify && scripts/boards/show_board_profile.sh` (`pre-board` contains the retarget; use its successor once merged) | exit 0; JSON lists 18 `UNMEASURED` values and 1 human decision | none |
| ARRIVAL-02 | HUMAN | Unbox; photograph both sides and the label; check the kit has the board, J15 22-pin cable, panel. Do not connect the PIR. | photos exist | `<BOARD_MARKING>` text |
| ARRIVAL-03 | HUMAN | SW5 = `0010`; connect J19 to host; open the console (`screen <SERIAL_DEVICE> 115200`); connect Ethernet; connect the PD charger to J2 | U-Boot then Linux login on the console | console log |
| ARRIVAL-04 | board (vendor) | log in as `root`; `cat /etc/os-release; uname -a; ip -4 addr` | vendor image boots to a shell | `<BOARD_ADDRESS>` |
| ARRIVAL-05 | host | `ssh root@<BOARD_ADDRESS> 'dd if=/dev/mmcblk2 bs=4M' \| zstd -o frdm-factory-emmc.img.zst` (confirm `mmcblk2` is the eMMC with `lsblk` first) | file size ≈ eMMC size compressed; keep outside the repo | backup SHA-256 |
| ARRIVAL-06 | host | `export BHP_BOARD_SSH_TARGET=root@<BOARD_ADDRESS>; scripts/deploy/frdm_evidence_bundle.sh push --execute` | JSON/log `frdm_evidence_bundle_pushed` | bundle SHA-256 |
| ARRIVAL-07 | board | `cd /root/bhp-evidence && sh hardware_tests/frdm/frdm_board_identity.sh --board-marking '<BOARD_MARKING>'` | `result` is `complete`, or `incomplete` only on `wifi_chip_name`; never `mismatch` (a `mismatch` on RAM/eMMC is the 2 GB/16 GB variant: stop, contact the seller) | IDENTITY record |
| ARRIVAL-08 | HUMAN | Power off. With a meter, measure the J18 I/O voltage on the candidate pin `<J18_PIN>` to GND. Take candidates in the order of [`frdm-j18-pir-candidates.json`](../hardware/frdm-j18-pir-candidates.json) and find each one's J18 pin in the FRDM-IMX8MPLUS schematic header table, board powered, nothing connected | reading taken | `<HEADER_MV>` |
| ARRIVAL-09 | board | `sh hardware_tests/frdm/frdm_j18_gpio_inventory.sh --candidate <J18_PIN>:<PAD_MUX> --measured-header-voltage-mv <HEADER_MV> --voltage-method '<METER_MODEL> J18-to-GND'` | `candidate_format`, `pad_mux_owner`, `gpio_line_consumer`, `header_io_voltage` all `pass` | PIR record (pin candidates) |
| ARRIVAL-10 | HUMAN | Power off. Wire the PIR: VCC per its datasheet, GND, OUT to `<J18_PIN>` (only if ARRIVAL-09 passed at 3.3 V) | wiring photo | wiring photo |
| ARRIVAL-11 | board | `sh hardware_tests/frdm/frdm_pir_edge_capture.sh --mode gpio --controller <gpioN> --line <L> --duration-seconds 60` (values from ARRIVAL-09 candidates); stay still, then walk past | `edges_observed` `pass`; `BHP_BOARD_PIR_ACTIVE_LEVEL` candidate present | PIR record |
| ARRIVAL-12 | board | `sh hardware_tests/frdm/frdm_mic_capture.sh --duration-seconds 5` with the CTIA headset plugged in; speak | `alsa_card`, `codec_i2c`, `capture_non_silent` `pass` | AUDIO record |
| ARRIVAL-13 | board | `sh hardware_tests/frdm/frdm_display_touch_backlight.sh` (vendor DTB with the Waveshare panel only if the vendor release provides one, `<VENDOR_PANEL_DTB>`) | records written; any `mismatch` is expected on a DTB without the panel | DISPLAY/TOUCH/BACKLIGHT records |
| ARRIVAL-14 | host | copy the ARRIVAL-09/11 candidates into `config/boards/frdm-imx8mp.env`; `make verify`; commit `docs(hardware): record FRDM PIR pin evidence` with the record paths | `verify_frdm_device_tree_overlay` logs `committed_pir_state":"measured"` | commit |
| ARRIVAL-15 | HUMAN | Decide on the NXP Software License Agreement for `firmware-imx` (LPDDR4 training) and the IW612 firmware. If accepted, record it as the next free ADR with `<!-- @canonical nxp-firmware-eula-acceptance -->` and `**Status:** accepted`, then set `BHP_BOARD_NXP_FIRMWARE_EULA=accepted-adr-<NNNN>` | ADR merged; `make verify` exit 0 | ADR |
| ARRIVAL-16 | host | `scripts/build/build_bringup_image.sh` (SSH bring-up profile env above) | `images/sdcard.img` and `images/imx8-boot-sd.bin` exist; post-image did not log `NXP_FIRMWARE_EULA_NOT_ACCEPTED` | build log |
| ARRIVAL-17 | HUMAN+host | SW5 = `0001`, J3 to host, power on; `uuu -b sd_all "$BHP_BUILDROOT_OUTPUT_DIR/images/imx8-boot-sd.bin" "$BHP_BUILDROOT_OUTPUT_DIR/images/sdcard.img"` with a microSD in the slot; power off; SW5 = `0011`; power on | console shows `frdm-imx8mp-house-panel-bringup` and the NON-PRODUCTION banner | console log |
| ARRIVAL-18 | host | `ssh -o IdentitiesOnly=yes -i <KEY> root@<BOARD_ADDRESS> cat /etc/beaglebone-house-panel/image-profile /etc/beaglebone-house-panel/image-version` | `bringup-root-public-key-only`; `board_id=frdm-imx8mp`; commit matches ARRIVAL-16 | image identity |
| ARRIVAL-19 | board | `frdm_evidence_bundle.sh push --execute` (host), then `sh hardware_tests/frdm/frdm_display_touch_backlight.sh --execute-test-pattern 30`; answer `yes` if the pattern shows | `first_frame_visible` `pass`: **panel shows first frame** | DISPLAY record |
| ARRIVAL-20 | board | `sh hardware_tests/frdm/frdm_display_touch_backlight.sh --execute-backlight-off-trial 5`; answer `dark`/`lit` | `backlight_zero_is_off` `pass` | BACKLIGHT record |
| ARRIVAL-21 | board | `sh hardware_tests/frdm/frdm_pir_edge_capture.sh --mode evdev --duration-seconds 60`; walk past | `pir_input_device`, `gpio_keys_wakeup`, `pir_irq_label`, `sw_front_proximity_events` `pass` | PIR record |
| ARRIVAL-22 | board | `sh hardware_tests/frdm/frdm_suspend_wake.sh` (preflight) | `power_state_mem`, `pir_wakeup_enabled`, `wake_irq_interface`, `rtc_safety_wake` `pass` | SUSPEND record |
| ARRIVAL-23 | board (console) | `sh hardware_tests/frdm/frdm_suspend_wake.sh --execute --mem-sleep deep --safety-rtc-seconds 120`; stay out of view ~10 s, then walk past | `suspend_entered` and `attributed_pir_wake` `pass`: **PIR wakes it**. If it woke on the RTC, repeat with `--mem-sleep s2idle` and record both | SUSPEND record (MEM_SLEEP_STATE candidate) |
| ARRIVAL-24 | board | `sh hardware_tests/frdm/frdm_display_touch_backlight.sh --execute-test-pattern 30` after the resume | `first_frame_visible` `pass` after wake | DISPLAY record |
| ARRIVAL-25 | host | `scripts/deploy/frdm_evidence_bundle.sh fetch --execute`; copy candidates into the profile; `make verify`; commit with record paths | `scripts/boards/show_board_profile.sh` no longer lists the closed keys | commit |

Acceptance counts for `FRDM-GATE-SUSPEND` (50 consecutive attributed trials
per claimed cell, then the masked-PIR soak) use the existing correlated runner,
`hardware_tests/scripts/run_suspend_wake_trials.sh`, on the bring-up image,
which includes standard-library `python3` for its classifier; see
`hardware_tests/README.md` "Suspend/wake trials".

Wi-Fi, tailnet, and power samples continue after ARRIVAL-25:

- Wi-Fi credentials never enter the repository. On the board:
  `wpa_passphrase '<SSID>' > /etc/wpa_supplicant.conf` (type the passphrase on
  stdin), `chmod 600 /etc/wpa_supplicant.conf`,
  `wpa_supplicant -B -i <WIFI_INTERFACE> -c /etc/wpa_supplicant.conf`,
  `udhcpc -i <WIFI_INTERFACE>`; then
  `sh hardware_tests/frdm/frdm_wifi_resume_timing.sh --observe-seconds 120`.
  For resume timing start
  `frdm_wifi_resume_timing.sh --observe-seconds 300 --after-next-resume` in one
  SSH session and run ARRIVAL-23 on the console.
- Tailscale comes from the image: the pinned official `1.102.2` arm64 archive
  (`bhp-tailscale-official`, ADR 0010) and `S50tailscaled`. The operator
  enrolls the node with a tag that carries the Lexinet node capability
  (`tailscale up`, interactive or a one-time key kept outside the repository).
  Its state stays in `/var/lib/tailscale` on the board, never in an image. Then
  add `--tailnet-probe <TAILNET_HOST>` and `--lexinet-url <LEXINET_URL>`.
- Power: for each of `active_day`, `active_night`, `backlight_zero`, `idle`,
  and `mem` (with `--mem-sleep-state`), record a meter trace
  (`elapsed_seconds,volts,amps`) and run
  `sh hardware_tests/frdm/frdm_power_input_log.sh --software-state <STATE> --board-revision <BOARD_REV> --accessory-revision '<PANEL_AND_PIR_ASSEMBLY>' --supply-description '<CHARGER_MODEL_AND_RATING>' --meter-csv <METER_CSV> --instrument '<METER_MODEL>'`.

## Arrival status (2026-10-05)

The board is on the Arch development PC (cole-arch-pc). These are observations
from the vendor image, not closed gates: no profile key changed and
`scripts/boards/show_board_profile.sh` still reports 18 `UNMEASURED` values and
1 human decision. Raw captures are in the ignored
`hardware_tests/results/frdm/health-check/` directories named below.

**Access, as connected.**

- Console: J19 (CH342, USB `1a86:55d2`) enumerates two CDC-ACM ports. `if00`
  is the Cortex-A53 console at 115200 (`dev.toml [hardware] console`, the
  `/dev/serial/by-id/` path; `/dev/ttyACM0` here); `if02` was silent. Since
  2026-10-10 the project agent has the board too (`access = "agent"`): an ACL
  on both of the bridge's ttys and `192.168.7.181:22` only (infra
  `scshafe-agent-setup hardware`); the owner-gated actions stay the owner's.
- Power: from the PC's USB-C. The Apple 65 W PD charger on J2 left U-Boot in a
  reset loop at USB XHCI initialisation (cause inferred, not measured).
  FRDM-GATE-POWER at J2 still needs a supply that boots it and an inline meter.
- LAN: the vendor image's `eth1` took the DHCP lease 192.168.7.181 (`eth0` was
  down); which RJ45 jack is which stays FRDM-GATE-WIFI inventory. The vendor
  image allowed root SSH with no password; since 2026-10-10 root is key-only
  (`/etc/ssh/sshd_config.d/10-key-only.conf`; the owner's and the agent's
  keys). The serial console is the fallback if a key is lost. Its login
  prompt says "Type 'root' to login with superuser privileges (no password
  will be asked)", so anyone at J19 gets root without a password
  (`panel-boot-20261011T003158Z/console.log`).

**Checklist.**

- ARRIVAL-01 passed (2026-10-04): 18 `UNMEASURED`, 1 human decision.
- ARRIVAL-02 photos done (2026-10-10; both sides, in the ignored
  `board-photos-20261010/`). Marking: "FRDM-IMX8MPLUS", "700-95022 REV B3",
  "SCH-95022 REV B3", "©2025 NXP", with a u-blox `MAYA-W276-00B` Wi-Fi module.
  The silkscreen boot table (USB `0001`, eMMC `0010`, Micro SD `0011`) matches
  the profile's SW5 values. The photos show the RJ45 MAC labels: the cabled
  jack, next to the USB-A port, is `10-07-23-6E-CC-0D`, which is `eth1` with
  carrier; the outer jack is `…-CC-0C`, which is `eth0` with no carrier. That
  answers the FRDM-GATE-WIFI question of which jack is which. The kit check
  (J15 22-pin cable, panel) is not in the photos and is still open.
- ARRIVAL-03 done on PC USB power instead of the PD charger (above). U-Boot
  2026.04-lf reported a bad environment CRC and used its default environment.
- ARRIVAL-04 done: "NXP i.MX Distro with Wayland and XWayland 6.18-wrynose",
  kernel 6.18.20-2.0.0, hostname `imx8mpevk`, model "NXP FRDM-IMX8MPLUS",
  3686668 kB RAM visible, eMMC `mmcblk2` 31268536320 B (DOS label: p1 83 MiB
  vfat boot, p2 9.8 GiB ext4 root, the rest unallocated), `boot0`/`boot1`
  4 MiB, QSPI `mtdblock0` 32 MiB.
- ARRIVAL-05 done: the factory backup is on the Arch PC, outside the repository,
  at `/data/backups/frdm-imx8mplus/factory-20261004/` (read-only files and
  `SHA256SUMS`): the raw eMMC image `32da8904…f0c5`, `boot0` = `boot1`
  `de326aef…6c43`, `mtdblock0` `fdc617b3…c642`. Restore procedure (written,
  not run): [frdm-factory-restore.md](frdm-factory-restore.md).
- ARRIVAL-06 done: the evidence bundle is in `/root/bhp-evidence` on the board.
- ARRIVAL-07 done (2026-10-10, `identity-20261010T235538Z/`): `complete`,
  8 of 8 checks pass: `fsl,frdm-imx8mp`, i.MX8MP (SoC rev 1.1), 4 CPUs,
  3686668 kB RAM, a 31268536320 B eMMC, and the IW612 on SDIO (`mmc0:0001`,
  `0x0471:0x0204`, `wlan_sdio`). This is the 4 GB / 32 GB variant, not the
  2 GB / 16 GB one. The marking was transcribed by the agent from the owner's
  photos.
- ARRIVAL-13 discovery ran on the vendor DTB (2026-10-10, below) and on the
  vendor panel DTB with the panel attached (2026-10-11, below). With the I2C
  wires on J27 (2026-10-18, below) the panel lights, touch reports and the
  backlight dims and goes dark. The gate script itself has not rerun since.
- ARRIVAL-08 onward otherwise: not started. ARRIVAL-08 and -10 need the owner at the
  board with a meter and the PIR; ARRIVAL-15 is the NXP firmware decision.

**Health check (2026-10-05, read-only).** `make verify-board`
(`scripts/deploy/frdm_board_health.sh`, the hardware gate; captures
`health-check/20261005T071412Z` by hand and `20261005T072816Z` by the script):

- Reachable over SSH and the console; up 8 h 48 min (booted about
  2026-10-04 22:26 UTC); load 0.00; SoC 40–41 °C idle.
- Factory storage unchanged: the MBR (partition table), the bootloader region
  before p1, the boot partition p1, `boot0`, `boot1` and the QSPI flash match the
  backup. p2, the root file system, is not compared: it changes in normal
  operation and holds the ARRIVAL-06 bundle.
- Failed units: `weston` (no display attached) and `imx8-isp` (no camera).
- Kernel log: `rtc-pcf2127` reports "oscillator stop detected" (the RTC time is
  unreliable until NTP sets it; matters for ARRIVAL-22's RTC safety wake), and the
  camera-module expander `adp5585` at `1-0034` is absent (`-ENXIO`), so its
  regulators and the camera defer. Nothing else at error level.
- A root login has been open on the serial console since 2026-10-04 22:51 UTC.

**For the owner to note.** The vendor image runs NXP's 6.18 line (kernel
6.18.20-2.0.0, U-Boot 2026.04-lf); the project still pins `lf-6.12.49-2.2.0`
for its own kernel, U-Boot and ATF. Vendor-image evidence comes from 6.18; a
claim about the bring-up image needs the project kernel. No pin was changed.

**2026-10-10, agent session before agent access (PC side only).** The agent
user could not reach the board yet, so `make verify-board` and display
discovery did not run then. On the PC,
`lsusb` lists `1a86:55d2`, and `/dev/ttyACM0` and `/dev/ttyACM1` exist (group
`uucp`). The `/dev/serial/by-id/` links date from 2026-10-04 17:17 local, so the
bridge has not re-enumerated since then. `cdc_acm` is loaded. A pacman upgrade at
2026-10-10 12:57 local (`linux` 7.2.8.arch1-2 to 7.2.9.arch1-1) removed the
running kernel's modules, so no module that is not already loaded can load until
the PC reboots. The board draws its power from the PC's USB-C, so a PC reboot
probably power-cycles it (unmeasured). That makes the reboot owner-gated: fetch
evidence and close the console session first.

**Health check (2026-10-10, read-only, agent session).** `make verify-board`
returned `complete` (capture `health-check/20261010T233218Z`, with
`kernel-log.txt`). Compared with 2026-10-05:

- Same boot: up 6 d 1 h (booted 2026-10-04 22:26 UTC), so the board was not
  power-cycled. Same OS (6.18-wrynose), kernel 6.18.20-2.0.0, load 0.00, SoC
  41 °C. The clock is NTP-synchronised. The console root login from
  2026-10-04 22:51 UTC is still open.
- Factory storage unchanged: `boot0`, `boot1`, QSPI, the MBR, the bootloader
  region before p1 and p1 all match the backup.
- Failed units: `imx8-isp`, `weston.service` and `weston.socket`. The socket
  fails along with the service; the 2026-10-05 note named only the service.
- Kernel log: unchanged in substance. It still has `rtc-pcf2127` (`rtc1`)
  "oscillator stop detected" and `adp5585` at `1-0034` `-ENXIO`, with the
  camera regulators deferred. `rtc0` is the SoC's `snvs_rtc`.
- From the agent account, `ping` is refused by the PC's egress filter
  (`sendmsg: Operation not permitted`); only SSH to `.181:22` is open, as
  granted.

**Display discovery (2026-10-10; not read-only, see the p1 correction below).**
`frdm_display_touch_backlight.sh` ran from `/root/bhp-evidence` with its
records in the board's `/tmp`. Records and the kernel's view are in
`display-discovery-20261010T233252Z/`. All three gates return `mismatch`, as
the checklist expects on a DTB without the panel:

- The running tree is the vendor `imx8mp-frdm.dtb` with U-Boot's fixups
  (decompiled and diffed; OP-TEE, the MACs, `bootargs`, memory). It enables
  HDMI only: `lcdif3` (`32fc6000`) feeds `dw-hdmi`, and `mipi_dsi` (`32e60000`),
  `lcdif1` and `lcdif2` are `disabled`.
- DRM: `card0` is the GPU (`galcore`, render node only). `card1` is `imx-drm`,
  with one connector, `HDMI-A-1` (id 37), `disconnected`. There is no DSI
  connector, no `/sys/class/backlight` entry and no `/dev/fb*`. The kernel logs
  "Cannot find any crtc or sizes".
- Touch: no Goodix input device. The input devices are the SNVS power key,
  `sw-keys` (gpio-keys), the HDMI jack and the WM8962 beeper.
- weston exits with status 1 five times at boot and hits the start-rate limit.
  Its own message is not in the journal, so "no usable output" is inferred,
  not shown.
- The vendor boot partition (p1) ships
  `imx8mp-frdm-waveshare-7inch-c-panel.dtb`. Compared with
  `imx8mp-frdm.dtb`, it enables `mipi_dsi` and `lcdif1` and adds a
  `waveshare,dsi2dpi` bridge at i2c3 `0x45` (2 data lanes). It also adds a
  Goodix `gt911` at i2c3 `0x14` with no interrupt property (polled), and a
  `panel-dpi` panel at 1024×600 with a 50 MHz pixel clock. It changes one
  `media_blk_ctrl` clock rate. This matches the profile's expectations
  (backlight `*-0045`, touch i2c3 `0x14`, polled). The GT911 node declares
  `touchscreen-size` 800×480, not 1024×600, which is an open question for
  FRDM-GATE-TOUCH. The vendor kernel has `waveshare-dsi`, `sec-dsim` and
  `panel-simple` built in, and `goodix_ts` as a module.
- Booting that DTB (U-Boot `fdtfile`, or a one-shot `setenv` without
  `saveenv`) needs a reboot and the panel on J15, so it is owner-gated.

**eMMC boot configuration (2026-10-10, read-only).** Captured from debugfs
(`ext-csd-20261010T233419Z-factory-20261004/`) with the board still in its
factory state: `PARTITION_CONFIG` (byte 179) = `0x00`, `BOOT_BUS_CONDITIONS`
(177) = `0x00`, `BOOT_CONFIG_PROT` = `0x00`, `EXT_CSD_REV` = `0x08`. The eMMC
is DV4032 (manfid `0x45`, 07/2025), running HS400 enhanced strobe, 8-bit, at
200 MHz. Boot from the boot partitions is not enabled, so the ROM boots
`imx-boot` from the user area at 32 KiB (IVT `d1 00 20 41`).
`boot0` and `boot1` hold the same `imx-boot` at offset 0, and its first
4064 KiB match the user-area copy. The image is about 2.2 MiB and contains
U-Boot 2026.04-lf. The restore procedure keeps this configuration
([frdm-factory-restore.md](frdm-factory-restore.md)).

**Correction: p1 changed on 2026-10-10.** The display discovery above copied
`imx8mp-frdm.dtb` and `imx8mp-frdm-waveshare-7inch-c-panel.dtb` with `scp`
from the mounted boot partition. p1 is vfat mounted `rw,relatime` at
`/run/media/mmcblk2p1`, so reading a file there writes its FAT last-access
date. Four bytes changed: the two files' access dates, from 2030-01-01 to
2026-10-10. That was a write to board storage, made without the owner's
go-ahead; the 2026-10-10 summary called the work read-only, which was wrong.
The owner chose to leave it. Since then `make verify-board` returns `mismatch`
on `storage_boot_partition` alone: p1 is
`079b71da025237b44bae0002643fc14561ea00f0a43c72e74e625427aab2a17c`, the
backup's `7fd910a2…c8f` (`health-check/20261011T002827Z` and, after the panel
boots below, `20261011T045740Z`: same p1 hash, every other storage check
passes). A restore ([frdm-factory-restore.md](frdm-factory-restore.md)) puts
the factory p1 back. To read a file from p1 without writing it, copy the raw
partition (`dd if=/dev/mmcblk2p1`) and extract on the PC; `ls` is safe,
opening a file is not.

**Waveshare panel (2026-10-11, owner-approved boots).** Raw records in
`panel-attached-20261011T002818Z/` and `panel-boot-20261011T003158Z/`
(console log, gate records, I2C probes, `bridge-regs.txt`).

- Kit: an orange FPC printed "MIPI-DSI-Cable-12cm" with a "22PIN 0.5mm" end, a
  white 15-pin FFC ("AWM 20624"), a short orange FPC and a 4-wire lead (red,
  black, white, yellow) with a 2×2 female housing at each end. The orange
  22-to-15 cable joins J15 to the panel. The panel's 2×3 header is labelled
  "SCL1 SDA1 GND" and "GND 5V 5V" ("FAN").
- Boot: the owner approved booting the panel DTB from the console. From the
  U-Boot prompt, `setenv fdtfile imx8mp-frdm-waveshare-7inch-c-panel.dtb` and
  `boot`, without `saveenv`, twice (00:33 and 01:05 UTC). `bootflow scan`
  finds no flows, so `bootcmd` falls through to `bsp_bootcmd`, whose
  `loadfdt` loads `${fdtfile}` from p1; the stored environment is unchanged.
  The two later boots used the default DTB.
- With the panel DTB, everything binds: `mipi_dsi` and `lcdif1` are `okay`; DRM
  `card1-DPI-1` is `connected` with mode 1024x600 (connector 40), and weston
  enables output `DPI-1`. The crtc is active at a 50 MHz pixel clock;
  `/dev/fb0` is 1024×600; `/sys/class/backlight/2-0045` is `raw`, max 255,
  brightness 255. i2c3 (`i2c-2`) has `2-0045` (`ws_dsi2dpi`), `2-0014`
  (`gt911`) and `2-001a` (`wm8962`). The connector is DPI, not DSI, because
  NXP's `waveshare-dsi.c` sets `bridge.type = DRM_MODE_CONNECTOR_DPI`. The
  gate script looked only for `cardN-DSI-M`, so its DISPLAY record says
  `mismatch`; it now accepts `DPI` too.
- The panel stayed dark, and neither panel chip answers on i2c3: Goodix logs
  "I2C communication failure: -6" at `0x14`, an address probe of `0x14` and
  `0x5d` finds nothing, and `i2cget` of bridge registers `0x80`, `0x81`,
  `0xaa`–`0xad`, `0xc0`, `0xc2` at `0x45` fails. The bridge driver ignores
  its register-write errors, and the backlight node registers even when
  every write fails. So a bound `2-0045` and a backlight node, and the BACKLIGHT
  record's `backlight_node` `pass`, do not show the panel is reachable.
- Power: on the first boot the panel had no 5 V. With 5V and GND wired from
  J18 pins 4 and 6 (positions taken from the quick-start guide's drawing), the
  owner saw the panel's LED light, but the screen stayed dark. No meter, so
  the panel voltage is unmeasured. The board runs on the PC's USB-C, below
  NXP's 12–20 V input; whether its 5 V rail holds 4.75–5.30 V at the panel's
  roughly 0.5 A is unmeasured. The owner turned the ribbon over at one end
  between boots; which end is not recorded.
- Why it is dark (inferred, not yet tested): the panel's I2C is not
  connected. UM12340 Rev 1.0 §4.1.2, Figure 7, wires the panel's SDA1, SCL1,
  GND and 5V to J27 (`I2C_CAN`, 2×5) with separate wires. The ribbon's I2C
  pins go to a different bus (I2C2), while NXP's panel DTB puts the bridge and
  touch on i2c3. Figure 7 shows positions, not pin numbers. An independent
  research report (2026-10-11, from the public SCH-95022 **Rev B1**
  schematic, sheet 21) gives J27-9 SDA, J27-7 SCL, J27-6 GND, J27-2 5V, which
  matches the figure's positions; B3 is unconfirmed. The four J27 pins are not
  one 2×2 block, so the kit lead cannot be used there; single jumpers are on
  order.
- Ribbon orientation, UM12340 §4.1.1, Figure 6: at the panel, "the conductive
  side is up and the stiffener side is down"; at J15, "the conductive side is
  right and stiffener side is at left". Both are relative to the figure. The
  check that needs no figure: with power off, open the latch and make the
  cable's exposed fingers face the connector's metal spring contacts.
- U-Boot logged "Arbitration lost" and "i2c_init_transfer: failed for chip
  0x25" once (second boot). `0x25` is the PCA9450 PMIC, which Linux then
  probed normally (`nxp-pca9450 0-0025`). Not seen on the other three boots.

**Panel on J27 (2026-10-18, owner-approved boot).** Raw records in
`panel-j27-20261018T050000Z/` (console log, `notes.txt`, two wiring photos).

- Wiring (owner, power off): the J18 pin 4/6 wires removed; four F/F jumpers
  from the panel's header to J27: SDA1 (gray) to pin 9, SCL1 (purple) to
  pin 7, the GND beside SCL1 (black) to pin 6, the 5V beside SDA1 (white) to
  pin 2. The owner read the silkscreen "9" beside gray and "2" beside white,
  so the B1 pin numbers hold on this B3 board.
- Before the panel boot, on the default DTB with only the wires changed,
  `i2cget` on i2c3 (`i2c-2`, `30a40000`) read `0x45` registers `0x80` = `0xff`
  and `0x81` = `0x80`, and `0x14` answered; on 2026-10-11 all of these
  failed. `0x5d` did not answer.
- The owner power-cycled the board; the console stopped autoboot, and
  `setenv fdtfile imx8mp-frdm-waveshare-7inch-c-panel.dtb` and `boot` ran
  once, without `saveenv`. `card1-DPI-1` is `connected`, `card1-HDMI-A-1`
  `disconnected`, the backlight reads 255, and weston started. The owner saw
  the screen lit.
- Touch: "Goodix-TS 2-0014: ID 911, version: 1060", input `event2`. `evtest`
  for 15 s recorded 7 touches with `ABS_MT_POSITION_X/Y` and `BTN_TOUCH`
  1/0. The axes run X 0–799, Y 0–479, not the panel's 1024×600; libinput
  scales the range to the output, and nobody has checked that taps land
  where touched.
- Backlight: `2-0045/brightness` set to 64, 0, 128, then 255 at 5 s steps.
  The owner saw it dim at 64, go dark at 0 and come back. `actual_brightness`
  only echoes the value written; the bridge has no read-back.
- These are console observations, not gate records: FRDM-GATE-DISPLAY, -TOUCH
  and -BACKLIGHT stay open until `frdm_display_touch_backlight.sh` runs with
  its trials (ARRIVAL-19/20).
- LAN: on this power-up `eth1` (MAC `…-CC-0D`, the same jack) came up as
  192.168.50.15/24, not 192.168.7.181. The agent's grant covers only the old
  address, so it used the console. The owner chose to keep the new address
  (2026-10-18): `dev.toml [hardware] ssh` is now `root@192.168.50.15`; the
  agent's host grant still names the old address until the owner changes it.
- Console: when the agent's logger opened `if00` (05:00:19Z) the board
  logged line noise and then `sysrq: HELP`, so the UART saw a break. A
  break followed by a SysRq command key would act on it (for example `b`
  reboots); HELP means none did.

**Panel UI demo (2026-10-18).** This is not gate evidence; the plan is
`.claude/plans/2026-10-18-frdm-panel-ui-demo.md`. Raw records are in
`hardware_tests/results/frdm/panel-demo-20261018T171850Z/`. The cross-built UI,
supervisor and fake PIR ran from `/tmp` on the vendor image with weston
stopped. The UI was rotated 180°. The owner confirmed Home, the vocabulary
cards and taps. Simulated presence dimmed and blanked the panel, then lit it
again. Weston came back after `stop`. Two findings:
- the backlight goes dark for 12–44 ms on every supervisor cycle, which the
  owner saw as flicker, because each cycle writes brightness 0 before it
  publishes;
- the demo's fixed noon turns into solar night about 10 h after it starts.

**J18 and J27 pin sources.** UM12340 Rev 1.0 says J18 carries I2S, UART, I2C
and GPIO but has no pin table. The quick-start guide's J18 drawing uses
Raspberry Pi names in the Pi layout: 5V on pins 2 and 4, 3V3 on 1, Ground on
6, and "GPIO 2 (RXC)" and "GPIO 3 (RXFS)" where a Pi has I2C on pins 3 and 5.
The vendor DTB muxes `SAI5_RXC` to PDM (`pdmgrp`), `SAI5_RXFS` to `pwm4`, and
i2c3 to the dedicated `I2C3_SCL`/`I2C3_SDA` pads (debugfs `pinmux-pins`). The
independent report instead cites the B1 schematic (sheet 21) for I2C3 at
3.3 V on J18 pins 3 and 5, with PWM4 and PDM on other J18 pins. These
disagree, and nobody has the B3 schematic: do not wire anything to J18 pins
3 or 5 until it settles which is right. The display uses J27, which NXP's own
figure shows.

**Next, from a session that reaches the board.** Read-only gate scripts
(for example ARRIVAL-09 once `<HEADER_MV>` is measured) can write
their records to the board's `/tmp` with `--output-directory`, so they need no
write to its storage. Everything in `dev.toml [hardware] owner_gated` (flashing,
storage writes, package installs, suspend or reboot, tailnet and Wi-Fi changes,
the firmware license) waits for the owner.

## Placeholders the user fills

| Placeholder | Source | Used in |
|---|---|---|
| `<SERIAL_DEVICE>` | host `ls /dev/cu.usbserial-*` | ARRIVAL-03 |
| `<BOARD_ADDRESS>` | console `ip -4 addr` or router lease | ARRIVAL-04 onward |
| `<BOARD_MARKING>` | board label/silkscreen | ARRIVAL-07 |
| `<J18_PIN>`, `<PAD_MUX>` | `<PAD_MUX>` from the ranked [J18 candidates](../hardware/frdm-j18-pir-candidates.json); `<J18_PIN>` from the FRDM-IMX8MPLUS schematic J18 table (native `GPIO1`–`GPIO5` pads only, never the PCAL6416A expanders) | ARRIVAL-08/09 |
| `<HEADER_MV>`, `<METER_MODEL>` | meter reading | ARRIVAL-09 |
| `<gpioN>`, `<L>` | ARRIVAL-09 candidates | ARRIVAL-11 |
| NXP firmware EULA decision | human; ADR | ARRIVAL-15 |
| `<BUILDROOT_2026_05_1_DIR>`, `<ABSOLUTE_BRINGUP_OUTPUT_DIR>`, `<KEY>` | host paths outside the repository | ARRIVAL-16/18 |
| `<VENDOR_PANEL_DTB>` | `imx8mp-frdm-waveshare-7inch-c-panel.dtb`, shipped on the factory p1 (2026-10-10) | ARRIVAL-13 |
| `<SSID>`, passphrase, `<WIFI_INTERFACE>` | operator (secret, board only) / Wi-Fi record | Wi-Fi |
| `<TAILNET_HOST>`, `<LEXINET_URL>` | operator | Wi-Fi |
| `<STATE>`, `<METER_CSV>`, `<BOARD_REV>`, `<PANEL_AND_PIR_ASSEMBLY>`, `<CHARGER_MODEL_AND_RATING>` | operator and meter | power |

## Known blockers (not closable from the repository)

- `NXP-FIRMWARE-EULA` (human decision): without it the project image stops in
  post-image and has no IW612 firmware.
- `FRDM-GATE-PIR`: the project DTB (`imx8mp-frdm-bhp-panel.dtb`) and the custom
  PIR overlay refuse to render until the seven PIR values are measured.
- `FRDM-GATE-BACKLIGHT`: no device-tree backlight node is added; the NXP
  `waveshare,dsi2dpi` bridge registers `<bus>-0045` (`2-0045` on the vendor
  panel DTB, 2026-10-11), and the project kernel carries a patch that checks
  that registration. The node registers even when the bridge does not answer
  on I2C, so only the ARRIVAL-20 trial closes it.
- `FRDM-GATE-TOUCH`: no GT911 interrupt line is justified from available
  documentation; the driver polls.

## Pre-board software changes

These changed after this document was first written. They move no gate and
close no profile key:

- **Services** ([`service-principals.json`](../../system_image/service-principals.json)):
  - `S31bhp-presence-sensor` (root) loads the PIR input driver.
  - `S50tailscaled` (root) runs the pinned archive.
  - `S70panel_supervisord` (root) restarts at most 3 times, 5 s apart.
  - `S75bhp-panel-ui` (UID 1102) stays disabled and fail-dark until
    `FRDM-GATE-DISPLAY`.
  - `S80bhp-vocabulary-panel` is retired; the vocabulary feature runs inside
    `panel_ui`.
- **Devices:** every input node is root-only, and only the supervisor opens raw
  touch. `panel_ui` receives normalized observations. After `FRDM-GATE-TOUCH`,
  copy the measured identity and raw bounds into the thirteen
  `BHP_PANEL_SUPERVISOR_TOUCH_*` fields ([supervisor
  README](../../panel_supervisor/README.md)). Sound nodes are `root:root 600`
  until the media broker is packaged. The evidence scripts run as root, so
  ARRIVAL-12 is unaffected.
- **Suspend:** every suspend the supervisor requests first verifies kernel-visible
  microphone capture closure. Leave `BHP_PANEL_SUPERVISOR_MEDIA_BROKER_UID` unset
  until the broker is packaged. ARRIVAL-22/23 call the evidence script directly,
  not the supervisor. Microphone rail-off remains an FRDM evidence item.
- **Study client:** `panel-ui.env` reserves the `study.v1` settings with
  `tailnet_node` authentication; no study credential is stored on the board
  (ADR 0010).
- **Versions:** the FRDM kernel stays NXP `lf-6.12.49-2.2.0` through bring-up.
  The Buildroot `2026.08` pin is applied in a Mac-local session before the next
  image build. The ARRIVAL-16 image is build evidence only until it boots.

**Amended 2026-09-26:** ARRIVAL-01 uses `pre-board`. ARRIVAL-08/09 take pad
candidates from the documentation-derived [J18 list](../hardware/frdm-j18-pir-candidates.json).
The Tailscale step follows ADR 0010. Added the pre-board software changes above.
