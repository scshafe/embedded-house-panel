# SPDX-License-Identifier: GPL-2.0-only

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

KBUILD_OPTIONS = M=$(CURDIR)
ifneq ($(strip $(ARCH)),)
KBUILD_OPTIONS += ARCH=$(ARCH)
endif
ifneq ($(strip $(CROSS_COMPILE)),)
KBUILD_OPTIONS += CROSS_COMPILE=$(CROSS_COMPILE)
endif

.PHONY: all modules overlays clean

all: modules overlays

modules:
	$(MAKE) -C "$(KERNEL_SRC)" $(KBUILD_OPTIONS) modules

overlays:
	$(MAKE) -C overlays KERNEL_SRC="$(KERNEL_SRC)" all

clean:
	$(MAKE) -C "$(KERNEL_SRC)" $(KBUILD_OPTIONS) clean
	$(MAKE) -C overlays clean
