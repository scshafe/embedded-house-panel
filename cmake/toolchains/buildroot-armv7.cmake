# DEPRECATED: BeagleBone Black (lost; ADR 0008). Kept so the history preset still
# configures. Processor mirrors config/boards/beaglebone-black.env.
set(BHP_BUILDROOT_TARGET_PROCESSOR arm)
include("${CMAKE_CURRENT_LIST_DIR}/buildroot-sdk.cmake")
