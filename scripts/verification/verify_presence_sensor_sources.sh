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

for required_source in "$driver" "$custom_overlay" "$stock_overlay" "$binding"; do
  test -s "$required_source" || {
    bhp_fail VERIFY_PRESENCE.SOURCE_MISSING "$required_source" "$TRACE_ID"
    exit 1
  }
done

for required_driver_symbol in \
  devm_gpiod_get \
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

grep -F 'compatible = "gpio-keys"' "$stock_overlay" >/dev/null
grep -F 'wakeup-source;' "$stock_overlay" >/dev/null
grep -F 'wakeup-source;' "$custom_overlay" >/dev/null

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
