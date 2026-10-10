<!-- @canonical frdm-factory-restore -->

# Restore the FRDM factory eMMC

Status: written 2026-10-10 and **never run**. Each step that changes the board
is owner-gated (`dev.toml [hardware] owner_gated`: flashing, writing its storage,
changing the boot switch, power-cycling). This page meets the baseline plan's
Step 2 requirement that the `uuu -b emmc_all` restore path be documented
([plan](../../.claude/plans/2026-09-25-frdm-hardware-baseline.md)). A
rehearsal (R1–R3) checks the backup on the PC without touching the board.

## The backup

ARRIVAL-05 ([frdm-bringup.md](frdm-bringup.md#arrival-status-2026-10-05))
wrote these files to `/data/backups/frdm-imx8mplus/factory-20261004/` on the
Arch PC, outside the repository. They are read-only, and `SHA256SUMS` lists
their hashes:

| File | Board source | Size (B) |
|---|---|---|
| `frdm-factory-emmc.img.zst` | `/dev/mmcblk2`, the whole eMMC user area | 31268536320 raw |
| `mmcblk2boot0.img` | `/dev/mmcblk2boot0`, the boot partition holding `imx-boot` | 4194304 |
| `mmcblk2boot1.img` | `/dev/mmcblk2boot1` (same hash as `boot0`) | 4194304 |
| `mtdblock0.img` | `/dev/mtdblock0`, the QSPI flash | 33554432 |
| `lsblk.txt` | the layout at backup time | |

`SHA256SUMS` lists both the compressed file and the decompressed raw image
(`32da8904…f0c5`, on a line marked `(raw, decompressed)`, so plain
`sha256sum -c` reports that line as missing).

Two facts limit what a restore gives back:

- The image was read with `dd` while the vendor root file system (p2) was
  mounted read-write. p2 is therefore only as consistent as after a power
  loss: expect an ext4 journal replay on first boot. R3 checks it before the
  backup is needed.
- A restore returns p2 to its 2026-10-04 state. Anything written since, such as
  `/root/bhp-evidence` (ARRIVAL-06), is gone; fetch evidence first
  (`scripts/deploy/frdm_evidence_bundle.sh fetch --execute`).

Not yet recorded: the eMMC `EXT_CSD` boot configuration (`PARTITION_CONFIG`,
byte 179; `BOOT_BUS_CONDITIONS`, byte 177), which selects the partition the
boot ROM reads. Capture it read-only now, while the factory state is intact
(owner session; root on the vendor image):

```sh
# On the board. If /sys/kernel/debug is not mounted, stop: mounting it is a change.
ls /sys/kernel/debug/mmc2/
ext_csd=$(cat /sys/kernel/debug/mmc2/mmc2:*/ext_csd)
printf 'partition_config=0x%s boot_bus_conditions=0x%s\n' \
  "$(printf '%s' "$ext_csd" | cut -c359-360)" "$(printf '%s' "$ext_csd" | cut -c355-356)"
```

Keep the output under `hardware_tests/results/frdm/` with the backup's date
in the name.

## Rehearsal on the PC (no board)

Run as `cole` on the Arch PC. These steps only read the backup; R2 needs
about 32 GB of free space for the raw image.

**R1. Check the stored hashes.**

```sh
cd /data/backups/frdm-imx8mplus/factory-20261004
sha256sum --check --ignore-missing SHA256SUMS
```

Pass when all five present files report `OK`.

**R2. Decompress, then check the raw image's hash.**

```sh
RESTORE_DIR=<DIRECTORY_WITH_32_GB_FREE>    # not inside the repository
zstd -dc /data/backups/frdm-imx8mplus/factory-20261004/frdm-factory-emmc.img.zst \
  | tee "$RESTORE_DIR/frdm-factory-emmc.img" | sha256sum
stat -c %s "$RESTORE_DIR/frdm-factory-emmc.img"
```

Pass when the hash is `32da8904927a5699e3eb0c075515e338318903fffeddd88db371e88ec067f0c5`
and the size is `31268536320`. Restore step 3 uses this file.

**R3. Check the file systems read-only.**

```sh
sudo losetup --read-only --find --partscan --show "$RESTORE_DIR/frdm-factory-emmc.img"   # prints /dev/loopN
sudo fsck.vfat -n /dev/loopNp1
sudo e2fsck -fn /dev/loopNp2
sudo losetup --detach /dev/loopN
```

Pass when `fsck.vfat` reports no errors and `e2fsck -n` reports at most the
journal recovery the live `dd` implies. More than that means the backup's p2
is damaged: record it, and treat a restore as giving back the bootloader and
p1 only.

## Restore (owner, at the board)

Keep the console (`dev.toml [hardware] console`, J19) open throughout.

**1. Install `uuu` on the PC.** It is not installed. NXP publishes it as
`mfgtools` on GitHub. A package install on the PC asks the owner first.

**2. Put the board in serial download mode.** Power off. Set SW5 to `0001`
(`BHP_BOARD_BOOT_SWITCH_USB_SERIAL_DOWNLOAD`), connect J3 to the PC, then power
on. Pass when `uuu -lsusb` lists the board.

**3. Read the built-in script, then write the image.**

```sh
uuu -bshow emmc_all                   # read it first: what it writes and which mmc commands it runs
uuu -b emmc_all /data/backups/frdm-imx8mplus/factory-20261004/mmcblk2boot0.img \
  "$RESTORE_DIR/frdm-factory-emmc.img"
```

`emmc_all` loads its first argument over USB as the bootloader. It writes the
second argument to the eMMC user area, writes the first argument to the boot
partition, and sets `PARTITION_CONFIG` with `mmc partconf`. On the i.MX 8M Plus
the boot ROM reads `imx-boot` from offset 0 of the eMMC boot partition, so the
`boot0` dump is a candidate bootloader. It carries trailing padding up to
4 MiB, and nobody has tried it over SDP. If uuu rejects it, use the
`imx-boot-*-frdm*` file of the NXP release that matches the factory image
("NXP i.MX Distro with Wayland and XWayland 6.18-wrynose", U-Boot
2026.04-lf). Step 6 restores the exact `boot0`/`boot1` bytes either way.

The user-area write covers the partition table, the bootloader region before
p1, p1, p2, and any U-Boot environment stored in the user area. At arrival the
environment failed its CRC and U-Boot used its defaults. The environment's
offset has not been measured.

**4. Set the boot switch back.** Power off. Set SW5 to `0010`
(`BHP_BOARD_BOOT_SWITCH_EMMC`), disconnect J3, then power on. Use the PC's
USB-C supply: the Apple PD charger on J2 reset-loops U-Boot. Pass when U-Boot
and then the vendor login appear on the console, after any ext4 journal replay.

**5. Compare with the backup.** From the PC, with `config/device.env` exported:

```sh
make verify-board
```

Pass when `storage_mbr_partition_table`, `storage_bootloader_region` and
`storage_boot_partition` pass. If `storage_boot0`, `storage_boot1` or
`storage_qspi` fail, go to step 6. Otherwise the restore is done; go to
step 7.

**6. Restore only what differs (owner, from the vendor image).** For a boot
partition that failed, `N` = 0 or 1:

```sh
ssh root@<BOARD_ADDRESS> 'echo 0 > /sys/block/mmcblk2bootN/force_ro &&
  dd of=/dev/mmcblk2bootN bs=1M conv=fsync; echo 1 > /sys/block/mmcblk2bootN/force_ro' \
  < /data/backups/frdm-imx8mplus/factory-20261004/mmcblk2bootN.img
```

If `storage_qspi` failed:

```sh
ssh root@<BOARD_ADDRESS> 'cat /proc/mtd'    # confirm mtd0 is the 32 MiB QSPI flash
ssh root@<BOARD_ADDRESS> 'cat > /tmp/mtdblock0.img' \
  < /data/backups/frdm-imx8mplus/factory-20261004/mtdblock0.img
ssh root@<BOARD_ADDRESS> 'flashcp -v /tmp/mtdblock0.img /dev/mtd0 && rm /tmp/mtdblock0.img'
```

`flashcp` erases, writes and verifies the flash. If the image has no
`flashcp` (mtd-utils), stop: installing it needs the owner's go-ahead. Then
rerun step 5.

**7. Record the restore.** Keep the console log and the step-5 record under
`hardware_tests/results/frdm/`. Rerun the `EXT_CSD` capture above and compare
it with the factory value. Add a dated line to the
[arrival status](frdm-bringup.md#arrival-status-2026-10-05) naming the record.
