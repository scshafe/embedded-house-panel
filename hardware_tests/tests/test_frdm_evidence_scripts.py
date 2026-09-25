#!/usr/bin/env python3
"""Host tests for hardware_tests/frdm/ evidence scripts against fixture sysfs trees.

Every fixture value here is invented test data (never hardware evidence) and must
never be copied into config/boards/. The tests prove the scripts' JSON contract,
their pass/fail/unobserved logic, and that emitted candidates satisfy the board
profile schema, without an FRDM board.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
FRDM = PROJECT_ROOT / "hardware_tests/frdm"
PROFILE = PROJECT_ROOT / "config/boards/frdm-imx8mp.env"
EVIDENCE_SCHEMA = PROJECT_ROOT / "hardware_tests/schemas/frdm_gate_evidence.schema.json"
POWER_SCHEMA = PROJECT_ROOT / "hardware_tests/schemas/power_measurement.schema.json"
BOARD_SCHEMA = PROJECT_ROOT / "config/boards/board-profile.schema.json"
# The target runs BusyBox ash; set BHP_FRDM_TEST_SHELL="busybox sh" (with BusyBox
# applets first in PATH) to exercise that shell too.
TEST_SHELL = os.environ.get("BHP_FRDM_TEST_SHELL", "sh")

try:  # Optional on developer hosts; the minimal checks below always run.
    import jsonschema
except ImportError:  # pragma: no cover - depends on host packages
    jsonschema = None


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative.lstrip("/")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def symlink(root: Path, relative: str, target: str) -> None:
    link = root / relative.lstrip("/")
    link.parent.mkdir(parents=True, exist_ok=True)
    target_path = root / target.lstrip("/")
    target_path.mkdir(parents=True, exist_ok=True)
    os.symlink(target_path, link)


def identity_fixture(root: Path, memtotal_kib: int = 3_900_000, emmc_sectors: int = 61_071_360) -> None:
    write(root, "/proc/device-tree/compatible", "fsl,frdm-imx8mp\0fsl,imx8mp\0")
    write(root, "/proc/device-tree/model", "NXP i.MX8MPlus FRDM board\0")
    write(root, "/sys/devices/soc0/soc_id", "i.MX8MP\n")
    write(root, "/sys/devices/soc0/revision", "1.1\n")
    write(root, "/proc/cpuinfo", "".join(f"processor\t: {n}\nBogoMIPS\t: 16.00\n\n" for n in range(4)))
    write(root, "/proc/meminfo", f"MemTotal:        {memtotal_kib} kB\nMemFree:          100 kB\n")
    card = "/sys/bus/platform/devices/30b60000.mmc/mmc_host/mmc2/mmc2:0001"
    write(root, f"{card}/type", "MMC\n")
    write(root, f"{card}/name", "FIXTURE\n")
    write(root, f"{card}/manfid", "0x000000\n")
    write(root, f"{card}/date", "01/2026\n")
    write(root, f"{card}/block/mmcblk2/size", f"{emmc_sectors}\n")
    sdio = "/sys/bus/platform/devices/30b40000.mmc/mmc_host/mmc0/mmc0:0001"
    write(root, f"{sdio}/type", "SDIO\n")
    write(root, f"{sdio}/vendor", "0xffff\n")
    write(root, f"{sdio}/device", "0xffff\n")
    write(root, "/proc/cmdline", "console=ttymxc1,115200 root=PARTUUID=b4f5a8e1-02 rootwait rw\n")
    write(root, "/fixture-dmesg.txt", "[    0.000000] Booting Linux on physical CPU 0x0\n")


def pinctrl_fixture(root: Path) -> None:
    write(root, "/sys/kernel/debug/pinctrl/30330000.pinctrl/pinmux-pins",
          "Pinmux settings per pin\n"
          "pin 0 (MX8MP_IOMUXC_GPIO1_IO00): (MUX UNCLAIMED) (GPIO UNCLAIMED)\n"
          "pin 10 (MX8MP_IOMUXC_GPIO1_IO10): 30680000.pwm (GPIO UNCLAIMED) function pwm3 group pwm3grp\n"
          "pin 120 (MX8MP_IOMUXC_SAI1_RXD1): (MUX UNCLAIMED) (GPIO UNCLAIMED)\n")
    write(root, "/sys/kernel/debug/gpio",
          "gpiochip3: GPIOs 608-639, parent: platform/30230000.gpio, 30230000.gpio:\n"
          " gpio-620             (                    |regulator-fixture   ) out hi\n")
    write(root, "/proc/device-tree/__symbols__/gpio1", "/soc@0/bus@30000000/gpio@30200000\0")
    write(root, "/proc/device-tree/__symbols__/gpio4", "/soc@0/bus@30000000/gpio@30230000\0")
    write(root, "/proc/device-tree/__symbols__/pwm3", "/soc@0/bus@30400000/pwm@30680000\0")
    write(root, "/sys/class/regulator/regulator.1/name", "FIXTURE_LDO\n")
    write(root, "/sys/class/regulator/regulator.1/microvolts", "3300000\n")


def display_fixture(root: Path) -> None:
    write(root, "/sys/class/drm/card1-DSI-1/status", "connected\n")
    write(root, "/sys/class/drm/card1-DSI-1/modes", "1024x600\n")
    write(root, "/sys/class/drm/card1-DSI-1/connector_id", "35\n")
    symlink(root, "/sys/class/drm/card1/device/driver", "/sys/bus/platform/drivers/fixture-lcdif")
    write(root, "/sys/class/graphics/fb0/name", "fixturedrmfb\n")
    write(root, "/sys/class/graphics/fb0/virtual_size", "1024,600\n")
    write(root, "/proc/device-tree/__symbols__/i2c3", "/soc@0/bus@30800000/i2c@30a40000\0")
    client = "/sys/devices/platform/soc@0/30a40000.i2c/i2c-2/2-0014"
    symlink(root, f"{client}/driver", "/sys/bus/i2c/drivers/goodix")
    symlink(root, f"{client}/of_node",
            "/sys/firmware/devicetree/base/soc@0/bus@30800000/i2c@30a40000/touchscreen@14")
    write(root, "/sys/class/input/event1/device/name", "Goodix Capacitive TouchScreen\n")
    write(root, "/sys/class/input/event1/device/phys", "input/ts\n")
    os.symlink(root / client.lstrip("/"), root / "sys/class/input/event1/device/device")
    write(root, "/proc/interrupts", "           CPU0\n 11:        0     GICv3  30 Level     arch_timer\n")
    write(root, "/sys/class/backlight/2-0045/type", "raw\n")
    write(root, "/sys/class/backlight/2-0045/max_brightness", "255\n")
    write(root, "/sys/class/backlight/2-0045/brightness", "255\n")


class FrdmEvidenceScriptTest(unittest.TestCase):
    """Fixture-backed contract tests; none of this is hardware evidence."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-frdm-evidence-")
        base = Path(self.temporary.name)
        self.root = base / "root"
        self.output = base / "results"
        self.root.mkdir()
        self.board_schema = json.loads(BOARD_SCHEMA.read_text(encoding="utf-8"))

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_script(self, name: str, *arguments: str, expect: int = 0) -> list[dict]:
        completed = subprocess.run(
            [*TEST_SHELL.split(), str(FRDM / name), "--fixture-root", str(self.root), "--output-directory",
             str(self.output), "--profile", str(PROFILE), *arguments],
            capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, expect, completed.stderr)
        records = [json.loads(Path(line).read_text(encoding="utf-8"))
                   for line in completed.stdout.splitlines() if line]
        for record in records:
            self.assert_record_contract(record)
        return records

    def assert_record_contract(self, record: dict) -> None:
        schema = json.loads(EVIDENCE_SCHEMA.read_text(encoding="utf-8"))
        if jsonschema is not None:
            jsonschema.validate(record, schema)
        self.assertEqual(set(record), set(schema["required"]))
        self.assertIs(record["gate_closed"], False)
        self.assertEqual(record["hardware_evidence"], "fixture_not_evidence")
        statuses = [check["status"] for check in record["checks"]]
        self.assertEqual(record["counts"], {s: statuses.count(s) for s in ("pass", "fail", "unobserved")})
        expected_result = ("mismatch" if "fail" in statuses else
                           "incomplete" if "unobserved" in statuses else "complete")
        self.assertEqual(record["result"], expected_result)
        for artifact in record["artifacts"]:
            self.assertRegex(artifact["sha256"], r"^[0-9a-f]{64}$")
        patterns = {**self.board_schema["required_keys"], **self.board_schema["required_keys_when_active"]}
        for candidate in record["candidates"]:
            self.assertIn(candidate["profile_key"], patterns)
            self.assertRegex(candidate["candidate_value"], patterns[candidate["profile_key"]])

    @staticmethod
    def checks(record: dict) -> dict[str, str]:
        return {check["id"]: check["status"] for check in record["checks"]}

    @staticmethod
    def candidates(record: dict) -> dict[str, str]:
        return {c["profile_key"]: c["candidate_value"] for c in record["candidates"]}

    def test_identity_matching_board_is_incomplete_only_on_operator_and_driver_items(self) -> None:
        identity_fixture(self.root)
        [record] = self.run_script("frdm_board_identity.sh")
        self.assertEqual(record["gate"], "FRDM-GATE-IDENTITY")
        checks = self.checks(record)
        for passing in ("dt_compatible", "soc_id", "cpu_count", "ram_capacity", "emmc_capacity", "wifi_sdio_card"):
            self.assertEqual(checks[passing], "pass", passing)
        self.assertEqual(checks["wifi_chip_name"], "unobserved")
        self.assertEqual(checks["board_markings"], "unobserved")
        self.assertEqual(record["result"], "incomplete")

    def test_identity_rejects_the_2gb_16gb_distributor_variant(self) -> None:
        identity_fixture(self.root, memtotal_kib=1_950_000, emmc_sectors=30_535_680)
        [record] = self.run_script("frdm_board_identity.sh", "--board-marking", "FIXTURE MARKING")
        checks = self.checks(record)
        self.assertEqual(checks["ram_capacity"], "fail")
        self.assertEqual(checks["emmc_capacity"], "fail")
        self.assertEqual(checks["board_markings"], "pass")
        self.assertEqual(record["result"], "mismatch")

    def test_j18_unclaimed_native_pad_yields_all_pir_pin_candidates(self) -> None:
        pinctrl_fixture(self.root)
        [record] = self.run_script(
            "frdm_j18_gpio_inventory.sh", "--candidate", "J18-7:MX8MP_IOMUXC_SAI1_RXD1__GPIO4_IO03",
            "--measured-header-voltage-mv", "3290", "--voltage-method", "fixture meter")
        self.assertEqual(record["result"], "complete")
        self.assertEqual(self.candidates(record), {
            "BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE": "none",
            "BHP_BOARD_PIR_HEADER_PIN": "J18-7",
            "BHP_BOARD_PIR_PAD_MUX": "MX8MP_IOMUXC_SAI1_RXD1__GPIO4_IO03",
            "BHP_BOARD_PIR_GPIO_CONTROLLER": "gpio4",
            "BHP_BOARD_PIR_GPIO_LINE": "3",
            "BHP_BOARD_HEADER_IO_VOLTAGE_MV": "3300",
        })

    def test_j18_owned_pad_names_the_node_to_disable_and_18v_fails(self) -> None:
        pinctrl_fixture(self.root)
        [record] = self.run_script(
            "frdm_j18_gpio_inventory.sh", "--candidate", "J18-12:MX8MP_IOMUXC_GPIO1_IO10__GPIO1_IO10",
            "--measured-header-voltage-mv", "1805", "--voltage-method", "fixture meter")
        checks = self.checks(record)
        self.assertEqual(checks["pad_mux_owner"], "pass")
        self.assertEqual(checks["header_io_voltage"], "fail")
        self.assertEqual(self.candidates(record)["BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE"], "pwm3")
        self.assertEqual(self.candidates(record)["BHP_BOARD_HEADER_IO_VOLTAGE_MV"], "1800")
        self.assertEqual(record["result"], "mismatch")

    def test_j18_expander_or_malformed_candidate_is_refused(self) -> None:
        pinctrl_fixture(self.root)
        [record] = self.run_script("frdm_j18_gpio_inventory.sh", "--candidate", "J18-7:pca6416_a:3")
        self.assertEqual(self.checks(record)["candidate_format"], "fail")
        self.assertEqual(record["candidates"], [])

    def test_display_touch_backlight_discovery_emits_three_gate_records(self) -> None:
        display_fixture(self.root)
        display, touch, backlight = self.run_script("frdm_display_touch_backlight.sh")
        self.assertEqual([r["gate"] for r in (display, touch, backlight)],
                         ["FRDM-GATE-DISPLAY", "FRDM-GATE-TOUCH", "FRDM-GATE-BACKLIGHT"])
        self.assertEqual(self.candidates(display), {
            "BHP_BOARD_DRM_DEVICE": "/dev/dri/card1",
            "BHP_BOARD_DRM_CONNECTOR_ID": "35",
            "BHP_BOARD_FBDEV_DEVICE": "/dev/fb0",
        })
        self.assertEqual(self.checks(display)["first_frame_visible"], "unobserved")
        self.assertEqual(self.candidates(touch), {
            "BHP_BOARD_TOUCH_INPUT_NAME": "Goodix%20Capacitive%20TouchScreen",
            "BHP_BOARD_TOUCH_INPUT_PHYS": "input/ts",
            "BHP_BOARD_TOUCH_INTERRUPT": "polled",
        })
        self.assertEqual(self.checks(touch)["touch_mode"], "pass")
        self.assertEqual(self.candidates(backlight),
                         {"BHP_BOARD_BACKLIGHT_SYSFS_DIR": "/sys/class/backlight/2-0045"})
        self.assertEqual(self.checks(backlight)["backlight_zero_is_off"], "unobserved")
        for record in (display, touch, backlight):
            self.assertEqual(record["result"], "incomplete")

    def test_missing_display_is_a_mismatch_not_a_guess(self) -> None:
        self.root.joinpath("sys/class").mkdir(parents=True)
        display, touch, backlight = self.run_script("frdm_display_touch_backlight.sh")
        for record in (display, touch, backlight):
            self.assertEqual(record["result"], "mismatch")
            self.assertEqual(record["candidates"], [])

    def test_power_meter_trace_becomes_a_schema_valid_v2_measurement(self) -> None:
        write(self.root, "/sys/class/thermal/thermal_zone0/type", "fixture-thermal\n")
        write(self.root, "/sys/class/thermal/thermal_zone0/temp", "41000\n")
        meter = write(self.root, "/meter.csv",
                      "elapsed_seconds,volts,amps\n0.0,20.01,0.310\n1.0,19.99,0.290\n2.0,20.00,0.300\n")
        [record] = self.run_script(
            "frdm_power_input_log.sh", "--software-state", "mem", "--mem-sleep-state", "deep",
            "--board-revision", "FIXTURE-A", "--accessory-revision", "FIXTURE-PANEL",
            "--supply-description", "fixture 65 W PD", "--meter-csv", str(meter),
            "--instrument", "fixture meter")
        self.assertEqual(record["result"], "complete")
        artifact = next(a for a in record["artifacts"] if a["path"].endswith("power_measurement.json"))
        measurement = json.loads((self.output / "frdm-gate-power" / artifact["path"]).read_text(encoding="utf-8"))
        if jsonschema is not None:
            jsonschema.validate(measurement, json.loads(POWER_SCHEMA.read_text(encoding="utf-8")))
        self.assertEqual(measurement["schema_version"], 2)
        self.assertEqual(measurement["sample_count"], 3)
        self.assertAlmostEqual(measurement["power_milliwatts"], 6000.0, delta=5.0)
        self.assertEqual(measurement["git_commit"], "not_a_project_image")

    def test_power_rejects_a_5v_contract_and_bad_traces(self) -> None:
        meter = write(self.root, "/meter.csv", "elapsed_seconds,volts,amps\n0,5.1,1.0\n1,5.0,1.0\n")
        [record] = self.run_script(
            "frdm_power_input_log.sh", "--software-state", "idle", "--board-revision", "A",
            "--accessory-revision", "B", "--supply-description", "fixture 5 V", "--meter-csv",
            str(meter), "--instrument", "fixture meter")
        self.assertEqual(self.checks(record)["pd_contract_voltage"], "fail")
        write(self.root, "/meter.csv", "elapsed_seconds,volts,amps\n1,20,1\n1,20,1\n")
        [bad] = self.run_script(
            "frdm_power_input_log.sh", "--software-state", "idle", "--board-revision", "A",
            "--accessory-revision", "B", "--supply-description", "x", "--meter-csv", str(meter),
            "--instrument", "fixture meter")
        self.assertEqual(self.checks(bad)["board_input_power_measured"], "fail")

    def test_board_only_scripts_refuse_fixture_roots_and_bad_usage(self) -> None:
        refusals = [
            ("frdm_pir_edge_capture.sh", "--mode", "evdev", "--duration-seconds", "5"),
            ("frdm_wifi_resume_timing.sh", "--observe-seconds", "30"),
            ("frdm_suspend_wake.sh", "--execute", "--mem-sleep", "deep", "--safety-rtc-seconds", "60"),
            ("frdm_display_touch_backlight.sh", "--execute-backlight-off-trial", "5"),
        ]
        for name, *arguments in refusals:
            self.run_script(name, *arguments, expect=2)
        for name, *arguments in [
            ("frdm_pir_edge_capture.sh", "--mode", "gpio", "--controller", "pca6416_a", "--line", "3",
             "--duration-seconds", "5"),
            ("frdm_suspend_wake.sh", "--execute", "--mem-sleep", "standby", "--safety-rtc-seconds", "60"),
            ("frdm_suspend_wake.sh", "--execute", "--mem-sleep", "deep", "--safety-rtc-seconds", "5"),
            ("frdm_mic_capture.sh", "--duration-seconds", "31"),
            ("frdm_power_input_log.sh", "--software-state", "mem", "--board-revision", "A",
             "--accessory-revision", "B", "--supply-description", "x"),
        ]:
            self.run_script(name, *arguments, expect=2)
        self.assertFalse(self.output.exists())

    def test_suspend_preflight_is_read_only_and_never_claims_a_wake(self) -> None:
        write(self.root, "/sys/power/state", "freeze mem\n")
        write(self.root, "/sys/power/mem_sleep", "s2idle [deep]\n")
        [record] = self.run_script("frdm_suspend_wake.sh")
        checks = self.checks(record)
        self.assertEqual(checks["power_state_mem"], "pass")
        self.assertEqual(checks["mem_sleep_deep_offered"], "pass")
        self.assertEqual(checks["attributed_pir_wake"], "unobserved")
        self.assertEqual(record["actions"], [])
        self.assertEqual(record["candidates"], [])

    def test_profile_percent_encoding_round_trips(self) -> None:
        script = (f'BHP_EVIDENCE_SCRIPT_DIRECTORY={FRDM}; . {FRDM}/lib/frdm_evidence.sh; '
                  'e=$(bhp_evidence_profile_encode "Goodix Capacitive TouchScreen"); '
                  'printf "%s\\n%s\\n" "$e" "$(bhp_evidence_profile_decode "$e")"')
        encoded, decoded = subprocess.run([*TEST_SHELL.split(), "-c", script], capture_output=True, text=True,
                                          check=True).stdout.splitlines()
        self.assertEqual(encoded, "Goodix%20Capacitive%20TouchScreen")
        self.assertEqual(decoded, "Goodix Capacitive TouchScreen")
        self.assertRegex(encoded, self.board_schema["required_keys_when_active"]["BHP_BOARD_TOUCH_INPUT_NAME"])

    def test_every_frdm_gate_has_an_evidence_script(self) -> None:
        adr = next(PROJECT_ROOT.glob("docs/decisions/0008-*.md")).read_text(encoding="utf-8")
        gates = set(re.findall(r"^\| `(FRDM-GATE-[A-Z0-9-]+)` \|", adr, re.MULTILINE))
        covered = set()
        for script in FRDM.glob("frdm_*.sh"):
            covered.update(re.findall(r"bhp_evidence_begin (FRDM-GATE-[A-Z0-9-]+)",
                                      script.read_text(encoding="utf-8")))
        self.assertEqual(gates, covered)


if __name__ == "__main__":
    sys.exit(unittest.main())
