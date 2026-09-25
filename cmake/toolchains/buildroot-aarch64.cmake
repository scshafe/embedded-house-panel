# Buildroot SDK toolchain for the active FRDM i.MX 8M Plus board (ADR 0008).
# Processor mirrors BHP_BOARD_CMAKE_SYSTEM_PROCESSOR in config/boards/frdm-imx8mp.env;
# the Cortex-A53 tuning is baked into the Buildroot toolchain wrapper.
set(BHP_BUILDROOT_TARGET_PROCESSOR aarch64)
include("${CMAKE_CURRENT_LIST_DIR}/buildroot-sdk.cmake")
