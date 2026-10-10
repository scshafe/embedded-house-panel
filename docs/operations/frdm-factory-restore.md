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

The factory eMMC boot configuration, captured read-only on 2026-10-10 while
the board was still in its factory state
(`hardware_tests/results/frdm/ext-csd-20261010T233419Z-factory-20261004/`;
summary in [frdm-bringup.md](frdm-bringup.md#arrival-status-2026-10-05)):

- `PARTITION_CONFIG` (byte 179) = `0x00` and `BOOT_BUS_CONDITIONS` (byte 177)
  = `0x00`. Boot from the boot partitions is not enabled, so the ROM boots
  `imx-boot` from the **user area at 32 KiB**. The user-area restore below
  rewrites that copy.
- `boot0` and `boot1` hold the same `imx-boot` (about 2.2 MiB, U-Boot
  2026.04-lf) at offset 0. Its bytes match the user-area copy. The factory
  board does not boot from them.

To repeat the capture (root on the board, read-only):

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

**3. Write the user area only, and keep the factory boot configuration.**
The built-in `emmc_all` script does two things the factory state does not
need. It writes the bootloader to a boot partition, and it runs
`mmc partconf` to boot from that partition. The factory board has
`PARTITION_CONFIG` = `0x00` and boots the user-area copy, so build a script
from the built-in one and change both:

```sh
uuu -bshow emmc_all > "$RESTORE_DIR/frdm-factory-restore.uuu"
# Edit the copy. Keep the SDP boot and the user-area write
# ("FB: flash -raw2sparse all _image"). Delete the line that flashes
# _flash.bin to the boot partition. Replace the `mmc partconf` line with:
#   FB: ucmd mmc partconf ${emmc_dev} 0 0 0
# Review the whole file before running it.
uuu -b "$RESTORE_DIR/frdm-factory-restore.uuu" \
  /data/backups/frdm-imx8mplus/factory-20261004/mmcblk2boot0.img \
  "$RESTORE_DIR/frdm-factory-emmc.img"
```

Not run. Check that this uuu accepts a script file after `-b` (`uuu -h`). The
first argument is the bootloader uuu loads over USB (SDP). The `boot0` dump
has the `imx-boot` IVT at offset 0 and the same bytes the factory boots from
the user area. It carries zero padding to 4 MiB, and nobody has tried it over
SDP. If uuu rejects it, use the `imx-boot-*-frdm*` file of the NXP release
that matches the factory image ("NXP i.MX Distro with Wayland and XWayland
6.18-wrynose", U-Boot 2026.04-lf). If the stock `emmc_all` runs by mistake,
the board still boots: `boot0` gets an `imx-boot` and `PARTITION_CONFIG`
points at it. Step 7's capture then shows the change. Set it back to `0x00`
from U-Boot with `mmc partconf <dev> 0 0 0` (owner-gated).

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
`hardware_tests/results/frdm/`. Rerun the `EXT_CSD` capture above. Pass when
`PARTITION_CONFIG` and `BOOT_BUS_CONDITIONS` are back to `0x00`. Add a dated line to the
[arrival status](frdm-bringup.md#arrival-status-2026-10-05) naming the record.
