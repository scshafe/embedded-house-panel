# Board profile: config/boards/<BR2_BHP_BOARD_ID>.env holds KEY=VALUE lines that
# make includes directly (scripts/verification/verify_board_profiles.py enforces
# the grammar). Project packages read the BHP_BOARD_* variables it defines.
BHP_BOARD_REQUESTED_ID := $(call qstrip,$(BR2_BHP_BOARD_ID))
ifneq ($(BHP_BOARD_REQUESTED_ID),)
BHP_BOARD_PROFILE_FILE := $(BR2_EXTERNAL_BHP_PATH)/../../config/boards/$(BHP_BOARD_REQUESTED_ID).env
ifeq ($(wildcard $(BHP_BOARD_PROFILE_FILE)),)
$(error BHP_BOARD.PROFILE_MISSING: BR2_BHP_BOARD_ID=$(BHP_BOARD_REQUESTED_ID) has no config/boards/$(BHP_BOARD_REQUESTED_ID).env)
endif
include $(BHP_BOARD_PROFILE_FILE)
ifneq ($(BHP_BOARD_ID),$(BHP_BOARD_REQUESTED_ID))
$(error BHP_BOARD.PROFILE_ID_MISMATCH: $(BHP_BOARD_PROFILE_FILE) declares $(BHP_BOARD_ID))
endif
endif

# Project packages call this before using board data, so Kconfig and
# show-targets stay usable without a board while builds fail closed.
define BHP_BOARD_REQUIRE_PROFILE
	test -n "$(BHP_BOARD_REQUESTED_ID)" || { \
		echo 'BHP_BOARD.PROFILE_UNSET: set BR2_BHP_BOARD_ID in the defconfig' >&2; \
		exit 1; \
	}
endef

include $(sort $(wildcard $(BR2_EXTERNAL_BHP_PATH)/package/*/*.mk))
