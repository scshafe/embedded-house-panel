#!/bin/sh
# Verifies both deliberately separate PIR wake experiment source trees.
# Inputs: optional KERNEL_DIR for an out-of-tree module build.
# Side effects: only a kernel module build when KERNEL_DIR is supplied.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_presence_sensor
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

driver=$PROJECT_ROOT/presence_sensor/experiments/custom_pir_input_driver/beagle_house_pir_input_driver.c
custom_overlay=$PROJECT_ROOT/presence_sensor/experiments/custom_pir_input_driver/beagle-house-custom-pir-overlay.dts
stock_overlay=$PROJECT_ROOT/presence_sensor/experiments/stock_gpio_keys_wake_baseline/beagle-house-gpio-keys-wake-baseline-overlay.dts
binding=$PROJECT_ROOT/presence_sensor/experiments/custom_pir_input_driver/Documentation/devicetree/bindings/input/beagle,house-pir.yaml
input_abi=$PROJECT_ROOT/presence_sensor/docs/input-abi.md
panel_evdev_source=$PROJECT_ROOT/panel_supervisor/src/evdev_presence_source.cpp

for required_source in \
  "$driver" \
  "$custom_overlay" \
  "$stock_overlay" \
  "$binding" \
  "$input_abi" \
  "$panel_evdev_source"; do
  test -s "$required_source" || {
    bhp_fail VERIFY_PRESENCE.SOURCE_MISSING "$required_source" "$TRACE_ID"
    exit 1
  }
done

for required_driver_symbol in \
  devm_gpiod_get \
  input_register_device \
  devm_request_threaded_irq \
  IRQF_TRIGGER_RISING \
  IRQF_TRIGGER_FALLING \
  dev_pm_set_wake_irq \
  pm_wakeup_event \
  MODULE_DEVICE_TABLE; do
  grep -F "$required_driver_symbol" "$driver" >/dev/null || {
    bhp_fail VERIFY_PRESENCE.DRIVER_CONTRACT_MISSING "$required_driver_symbol" "$TRACE_ID"
    exit 1
  }
done

if ! awk '
  /^[[:space:]]*error[[:space:]]*=[[:space:]]*input_register_device[[:space:]]*\([[:space:]]*pir->input[[:space:]]*\)[[:space:]]*;[[:space:]]*$/ {
    input_register_count++
    input_register_line = NR
  }
  /^[[:space:]]*error[[:space:]]*=[[:space:]]*devm_request_threaded_irq[[:space:]]*\([[:space:]]*device[[:space:]]*,/ {
    irq_request_count++
    irq_request_line = NR
  }
  END {
    exit !(input_register_count == 1 &&
           irq_request_count == 1 &&
           input_register_line < irq_request_line)
  }
' "$driver"; then
  bhp_fail VERIFY_PRESENCE.INPUT_IRQ_ORDER_INVALID \
    'expected exactly one input registration before exactly one threaded IRQ request' \
    "$TRACE_ID"
  exit 1
fi

grep -F 'compatible = "gpio-keys"' "$stock_overlay" >/dev/null
grep -F 'wakeup-source;' "$stock_overlay" >/dev/null
grep -F 'wakeup-source;' "$custom_overlay" >/dev/null

event_code_name=SW_FRONT_PROXIMITY
event_code_number=0x0b
event_type_name=EV_SW
event_type_number=0x05

grep -F "| Event type | \`$event_type_name\` (\`$event_type_number\`) |" \
  "$input_abi" >/dev/null || {
  bhp_fail VERIFY_PRESENCE.ABI_EVENT_TYPE_MISMATCH \
    "$event_type_name/$event_type_number is not the documented event type" "$TRACE_ID"
  exit 1
}
grep -F "| Event code | \`$event_code_name\` (\`$event_code_number\`) |" \
  "$input_abi" >/dev/null || {
  bhp_fail VERIFY_PRESENCE.ABI_EVENT_CODE_MISMATCH \
    "$event_code_name/$event_code_number is not the documented event code" "$TRACE_ID"
  exit 1
}

grep -F 'compatible = "beagle,house-pir";' "$custom_overlay" >/dev/null || {
  bhp_fail VERIFY_PRESENCE.CUSTOM_OVERLAY_COMPATIBLE_MISSING \
    'custom overlay must bind beagle,house-pir' "$TRACE_ID"
  exit 1
}
grep -F '{ .compatible = "beagle,house-pir" },' "$driver" >/dev/null || {
  bhp_fail VERIFY_PRESENCE.CUSTOM_DRIVER_OF_MATCH_MISSING \
    'driver OF match must bind the custom overlay compatible' "$TRACE_ID"
  exit 1
}
if grep -Eq '^[[:space:]]*linux,code[[:space:]]*=' "$custom_overlay"; then
  bhp_fail VERIFY_PRESENCE.CUSTOM_OVERLAY_EVENT_CODE_FORBIDDEN \
    'custom overlay delegates its fixed event code to the matched driver' "$TRACE_ID"
  exit 1
fi

for custom_driver_contract in \
  'input_set_capability(pir->input, EV_SW, SW_FRONT_PROXIMITY);' \
  'input_report_switch(pir->input, SW_FRONT_PROXIMITY, !!state);'; do
  grep -F "$custom_driver_contract" "$driver" >/dev/null || {
    bhp_fail VERIFY_PRESENCE.CUSTOM_DRIVER_EVENT_CODE_MISMATCH \
      "$custom_driver_contract" "$TRACE_ID"
    exit 1
  }
done

for stock_overlay_contract in \
  '#include <dt-bindings/input/input.h>' \
  'linux,input-type = <EV_SW>;' \
  'linux,code = <SW_FRONT_PROXIMITY>;'; do
  grep -F "$stock_overlay_contract" "$stock_overlay" >/dev/null || {
    bhp_fail VERIFY_PRESENCE.STOCK_OVERLAY_EVENT_CODE_MISMATCH \
      "$stock_overlay_contract" "$TRACE_ID"
    exit 1
  }
done

for panel_evdev_contract in \
  'EVIOCGBIT(EV_SW, sizeof(capability_words))' \
  'bit_is_set(capability_words, SW_FRONT_PROXIMITY)' \
  'EVIOCGSW(sizeof(state_words))' \
  'bit_is_set(state_words, SW_FRONT_PROXIMITY)'; do
  grep -F "$panel_evdev_contract" "$panel_evdev_source" >/dev/null || {
    bhp_fail VERIFY_PRESENCE.PANEL_EVENT_CODE_MISMATCH \
      "$panel_evdev_contract" "$TRACE_ID"
    exit 1
  }
done

bhp_log info presence_event_abi_verified "$TRACE_ID" \
  event_type "$event_type_name" event_code "$event_code_name" event_code_number "$event_code_number" \
  custom_overlay_code_source matched_driver stock_overlay_code_source linux_code_property

if [ -n "${KERNEL_DIR:-}" ]; then
  test -f "$KERNEL_DIR/Makefile" || {
    bhp_fail VERIFY_PRESENCE.KERNEL_DIR_INVALID "$KERNEL_DIR" "$TRACE_ID"
    exit 1
  }
  make -C "$KERNEL_DIR" M="$PROJECT_ROOT/presence_sensor" W=1 modules
  bhp_log info presence_kernel_module_compiled "$TRACE_ID" kernel_directory "$KERNEL_DIR"
else
  bhp_log warn presence_kernel_compile_skipped "$TRACE_ID" reason KERNEL_DIR_not_set
fi

bhp_log info presence_source_verification_complete "$TRACE_ID" variants custom_module_and_gpio_keys
