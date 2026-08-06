# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
DTC ?= dtc

OVERLAY_CPP_FLAGS := -nostdinc -undef -D__DTS__ -x assembler-with-cpp
OVERLAY_CPP_FLAGS += -I$(KERNEL_SRC)/include
DTC_FLAGS ?= -@

CUSTOM_PIR_SOURCE := ../experiments/custom_pir_input_driver/beagle-house-custom-pir-overlay.dts
GPIO_KEYS_BASELINE_SOURCE := ../experiments/stock_gpio_keys_wake_baseline/beagle-house-gpio-keys-wake-baseline-overlay.dts
OVERLAYS := beagle-house-custom-pir.dtbo
OVERLAYS += beagle-house-gpio-keys-wake-baseline.dtbo

.PHONY: all clean

all: $(OVERLAYS)

beagle-house-custom-pir.dtbo: $(CUSTOM_PIR_SOURCE)
	$(CPP) $(OVERLAY_CPP_FLAGS) "$<" | $(DTC) $(DTC_FLAGS) -I dts -O dtb -o "$@" -

beagle-house-gpio-keys-wake-baseline.dtbo: $(GPIO_KEYS_BASELINE_SOURCE)
	$(CPP) $(OVERLAY_CPP_FLAGS) "$<" | $(DTC) $(DTC_FLAGS) -I dts -O dtb -o "$@" -

clean:
	$(RM) $(OVERLAYS)
