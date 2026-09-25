#!/bin/sh
# Proves the FRDM overlay templates fail closed on UNMEASURED PIR values and
# render deterministically from test-only fixture profiles.
# Inputs: optional BHP_LINUX_IMX_DIR (a nxp-imx/linux-imx lf-6.12.49-2.2.0 tree
#   with arch/arm64/boot/dts and include/dt-bindings) to also compile the rendered
#   overlays with cpp/dtc and apply them to the NXP base DTB with fdtoverlay.
# Side effects: creates and removes one temporary directory. Idempotent: yes.
# Fixture values in scripts/verification/fixtures/board_profiles/ are NOT
# hardware evidence and must never be copied into config/boards/.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_frdm_device_tree_overlay
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

renderer=$PROJECT_ROOT/scripts/boards/render_device_tree_overlay.sh
project_template=$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/frdm-imx8mp/dts/bhp-frdm-imx8mp-panel.dtso.in
custom_template=$PROJECT_ROOT/presence_sensor/experiments/custom_pir_input_driver/frdm-imx8mp-custom-pir-overlay.dtso.in
fixture_directory=$PROJECT_ROOT/scripts/verification/fixtures/board_profiles

temporary_parent=${TMPDIR:-/tmp}
case "$temporary_parent" in
  /*) ;;
  *) bhp_fail VERIFY_FRDM_OVERLAY.TMPDIR_INVALID "TMPDIR must be absolute" "$TRACE_ID"; exit 1 ;;
esac
temporary_root=$(mktemp -d "$temporary_parent/bhp-frdm-overlay.XXXXXX")
cleanup()
{
  find "$temporary_root" -type f -exec rm -- {} \; 2>/dev/null || true
  find "$temporary_root" -depth -type d -exec rmdir -- {} \; 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

for template_contract in \
  'compatible = "gpio-keys";' \
  'linux,input-type = <EV_SW>;' \
  'linux,code = <SW_FRONT_PROXIMITY>;' \
  'wakeup-source;' \
  'wakeup-event-action = <EV_ACT_ANY>;' \
  'label = "@BHP_BOARD_PIR_INPUT_NAME@";' \
  'gpios = <&@BHP_BOARD_PIR_GPIO_CONTROLLER@ @BHP_BOARD_PIR_GPIO_LINE@ @BHP_DERIVED_PIR_GPIO_FLAGS@>;' \
  'FRDM-GATE-BACKLIGHT: deliberately no backlight node.' \
  'FRDM-GATE-TOUCH: documented gap.'; do
  grep -F -- "$template_contract" "$project_template" >/dev/null || {
    bhp_fail VERIFY_FRDM_OVERLAY.PROJECT_TEMPLATE_CONTRACT_MISSING \
      "$template_contract" "$TRACE_ID"
    exit 1
  }
done
for template_contract in \
  'compatible = "beagle,house-pir";' \
  'presence-gpios = <&@BHP_BOARD_PIR_GPIO_CONTROLLER@ @BHP_BOARD_PIR_GPIO_LINE@ @BHP_DERIVED_PIR_GPIO_FLAGS@>;' \
  'label = "@BHP_BOARD_PIR_CUSTOM_INPUT_NAME@";' \
  'wakeup-source;'; do
  grep -F -- "$template_contract" "$custom_template" >/dev/null || {
    bhp_fail VERIFY_FRDM_OVERLAY.CUSTOM_TEMPLATE_CONTRACT_MISSING \
      "$template_contract" "$TRACE_ID"
    exit 1
  }
done

# Negative control: the committed profile must refuse while FRDM-GATE-PIR is open.
pir_state=$("$PROJECT_ROOT/scripts/boards/show_board_profile.sh" --board frdm-imx8mp |
  python3 -c 'import json,sys; d=json.load(sys.stdin); print("unmeasured" if any(p["key"]=="BHP_BOARD_PIR_GPIO_LINE" for p in d["placeholders"]) else "measured")')
for template in "$project_template" "$custom_template"; do
  refusal_log=$temporary_root/refusal.log
  if "$renderer" --board frdm-imx8mp --template "$template" \
    --output "$temporary_root/refused.dtso" >"$refusal_log" 2>&1; then
    refusal_status=0
  else
    refusal_status=$?
  fi
  if [ "$pir_state" = unmeasured ]; then
    if [ "$refusal_status" -ne 3 ] ||
       ! grep -F 'BOARD_PROFILE.VALUE_UNMEASURED' "$refusal_log" >/dev/null ||
       ! grep -F 'FRDM-GATE-PIR' "$refusal_log" >/dev/null ||
       [ -e "$temporary_root/refused.dtso" ]; then
      bhp_fail VERIFY_FRDM_OVERLAY.UNMEASURED_PIN_RENDERED \
        "${template##*/} must refuse while FRDM-GATE-PIR is unmeasured" "$TRACE_ID"
      exit 1
    fi
  else
    [ "$refusal_status" -eq 0 ] || {
      bhp_fail VERIFY_FRDM_OVERLAY.MEASURED_PROFILE_REJECTED "${template##*/}" "$TRACE_ID"
      exit 1
    }
  fi
done

render_fixture()
{
  fixture_name=$1
  "$renderer" --profile "$fixture_directory/$fixture_name.env" \
    --template "$project_template" --output "$temporary_root/$fixture_name.dtso" >/dev/null
  "$renderer" --profile "$fixture_directory/$fixture_name.env" \
    --template "$custom_template" --output "$temporary_root/$fixture_name-custom.dtso" >/dev/null
}
render_fixture frdm-imx8mp-overlay-fixture
render_fixture frdm-imx8mp-overlay-owned-pad-fixture
for rendered_contract in \
  'frdm-imx8mp-overlay-fixture.dtso:gpios = <&gpio4 3 GPIO_ACTIVE_HIGH>;' \
  'frdm-imx8mp-overlay-fixture.dtso:MX8MP_IOMUXC_SAI1_RXD1__GPIO4_IO03	0x180' \
  'frdm-imx8mp-overlay-owned-pad-fixture.dtso:gpios = <&gpio1 10 GPIO_ACTIVE_LOW>;' \
  'frdm-imx8mp-overlay-owned-pad-fixture.dtso:&pwm3 {' \
  'frdm-imx8mp-overlay-owned-pad-fixture-custom.dtso:presence-gpios = <&gpio1 10 GPIO_ACTIVE_LOW>;'; do
  rendered_file=${rendered_contract%%:*}
  rendered_text=${rendered_contract#*:}
  grep -F -- "$rendered_text" "$temporary_root/$rendered_file" >/dev/null || {
    bhp_fail VERIFY_FRDM_OVERLAY.RENDER_CONTRACT_MISSING "$rendered_contract" "$TRACE_ID"
    exit 1
  }
done
if grep -F '&pwm3' "$temporary_root/frdm-imx8mp-overlay-fixture.dtso" >/dev/null; then
  bhp_fail VERIFY_FRDM_OVERLAY.UNEXPECTED_PAD_OWNER_FRAGMENT "none must not disable a node" "$TRACE_ID"
  exit 1
fi

compiled=not_attempted
if [ -n "${BHP_LINUX_IMX_DIR:-}" ]; then
  for required_command in cpp dtc fdtoverlay fdtget; do
    command -v "$required_command" >/dev/null 2>&1 || {
      bhp_fail VERIFY_FRDM_OVERLAY.DEPENDENCY_MISSING "$required_command" "$TRACE_ID"
      exit 1
    }
  done
  dts_directory=$BHP_LINUX_IMX_DIR/arch/arm64/boot/dts/freescale
  if [ ! -f "$dts_directory/imx8mp-frdm.dts" ] ||
     [ ! -f "$dts_directory/imx8mp-frdm-waveshare-7inch-c-panel.dtso" ]; then
    bhp_fail VERIFY_FRDM_OVERLAY.LINUX_TREE_INVALID "$BHP_LINUX_IMX_DIR" "$TRACE_ID"
    exit 1
  fi
  preprocess()
  {
    cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
      -I "$BHP_LINUX_IMX_DIR/include" -I "$dts_directory" \
      -I "$BHP_LINUX_IMX_DIR/scripts/dtc/include-prefixes" "$1" >"$2"
  }
  preprocess "$dts_directory/imx8mp-frdm.dts" "$temporary_root/base.pp"
  dtc -@ -q -I dts -O dtb -o "$temporary_root/base.dtb" "$temporary_root/base.pp"
  preprocess "$dts_directory/imx8mp-frdm-waveshare-7inch-c-panel.dtso" "$temporary_root/panel.pp"
  dtc -@ -q -I dts -O dtb -o "$temporary_root/panel.dtbo" "$temporary_root/panel.pp"
  fdtoverlay -i "$temporary_root/base.dtb" -o "$temporary_root/vendor.dtb" \
    "$temporary_root/panel.dtbo"
  for fixture_name in frdm-imx8mp-overlay-fixture frdm-imx8mp-overlay-owned-pad-fixture; do
    for overlay_variant in "$fixture_name" "$fixture_name-custom"; do
      preprocess "$temporary_root/$overlay_variant.dtso" "$temporary_root/$overlay_variant.pp"
      dtc -@ -I dts -O dtb -o "$temporary_root/$overlay_variant.dtbo" \
        "$temporary_root/$overlay_variant.pp" 2>"$temporary_root/$overlay_variant.dtc.log"
      if [ -s "$temporary_root/$overlay_variant.dtc.log" ]; then
        bhp_fail VERIFY_FRDM_OVERLAY.DTC_WARNING \
          "$overlay_variant: $(sed -n '1p' "$temporary_root/$overlay_variant.dtc.log")" "$TRACE_ID"
        exit 1
      fi
      fdtoverlay -i "$temporary_root/vendor.dtb" -o "$temporary_root/$overlay_variant.merged.dtb" \
        "$temporary_root/$overlay_variant.dtbo"
    done
    [ "$(fdtget -t s "$temporary_root/$fixture_name.merged.dtb" /bhp-presence-pir compatible)" = gpio-keys ] || {
      bhp_fail VERIFY_FRDM_OVERLAY.MERGED_NODE_MISSING "$fixture_name" "$TRACE_ID"
      exit 1
    }
  done
  [ "$(fdtget -t s "$temporary_root/frdm-imx8mp-overlay-owned-pad-fixture.merged.dtb" \
    /soc@0/bus@30400000/pwm@30680000 status)" = disabled ] || {
    bhp_fail VERIFY_FRDM_OVERLAY.PAD_OWNER_NOT_DISABLED pwm3 "$TRACE_ID"
    exit 1
  }
  compiled=applied_to_vendor_dtb
fi

bhp_log info frdm_device_tree_overlay_verified "$TRACE_ID" \
  committed_pir_state "$pir_state" \
  fixtures_rendered 4 \
  vendor_dtb_application "$compiled" \
  hardware_evidence not_performed
