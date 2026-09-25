# Host-side AArch64 verification toolchain for GNU/Linux cross compilers such as
# Debian/Ubuntu gcc-aarch64-linux-gnu. It proves the applications compile and
# link for the FRDM CPU without a Buildroot SDK; it never produces image
# artifacts. scripts/verification/verify_aarch64_cross_build.sh is the caller.
#
# Environment:
#   BHP_AARCH64_CROSS_PREFIX  compiler prefix (required), e.g. aarch64-linux-gnu
#   BHP_AARCH64_TARGET_CPU    -mcpu value from BHP_BOARD_TARGET_CPU (required)
#   BHP_AARCH64_SYSROOT       optional absolute prefix holding target-only
#                             libraries (usr/include, usr/lib, pkgconfig)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

foreach(required_variable BHP_AARCH64_CROSS_PREFIX BHP_AARCH64_TARGET_CPU)
  if(NOT DEFINED ENV{${required_variable}} OR "$ENV{${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()
set(BHP_AARCH64_CROSS_PREFIX "$ENV{BHP_AARCH64_CROSS_PREFIX}")
set(BHP_AARCH64_TARGET_CPU "$ENV{BHP_AARCH64_TARGET_CPU}")
if(NOT BHP_AARCH64_CROSS_PREFIX MATCHES "^aarch64-[A-Za-z0-9._+-]+$")
  message(FATAL_ERROR "BHP_AARCH64_CROSS_PREFIX must start with aarch64-")
endif()
if(NOT BHP_AARCH64_TARGET_CPU MATCHES "^cortex-a[0-9]+$")
  message(FATAL_ERROR "BHP_AARCH64_TARGET_CPU must be a cortex-aNN name")
endif()

find_program(BHP_AARCH64_CXX "${BHP_AARCH64_CROSS_PREFIX}-g++" NO_CMAKE_FIND_ROOT_PATH)
find_program(BHP_AARCH64_CC "${BHP_AARCH64_CROSS_PREFIX}-gcc" NO_CMAKE_FIND_ROOT_PATH)
if(NOT BHP_AARCH64_CXX OR NOT BHP_AARCH64_CC)
  message(FATAL_ERROR "${BHP_AARCH64_CROSS_PREFIX}-gcc/g++ were not found on PATH")
endif()
set(CMAKE_C_COMPILER "${BHP_AARCH64_CC}" CACHE FILEPATH "AArch64 C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${BHP_AARCH64_CXX}" CACHE FILEPATH "AArch64 C++ compiler" FORCE)
set(CMAKE_C_FLAGS_INIT "-mcpu=${BHP_AARCH64_TARGET_CPU}")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=${BHP_AARCH64_TARGET_CPU}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)

if(DEFINED ENV{BHP_AARCH64_SYSROOT} AND NOT "$ENV{BHP_AARCH64_SYSROOT}" STREQUAL "")
  set(BHP_AARCH64_SYSROOT "$ENV{BHP_AARCH64_SYSROOT}")
  if(NOT IS_ABSOLUTE "${BHP_AARCH64_SYSROOT}" OR NOT IS_DIRECTORY "${BHP_AARCH64_SYSROOT}")
    message(FATAL_ERROR "BHP_AARCH64_SYSROOT must be an existing absolute directory")
  endif()
  # Target libraries live in a prefix, not a full sysroot: glibc still comes
  # from the cross compiler's own sysroot.
  set(CMAKE_FIND_ROOT_PATH "${BHP_AARCH64_SYSROOT}")
  set(CMAKE_PREFIX_PATH "${BHP_AARCH64_SYSROOT}/usr" CACHE STRING "Target-only prefix" FORCE)
  set(ENV{PKG_CONFIG_PATH} "")
  set(ENV{PKG_CONFIG_DIR} "")
  set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
  set(ENV{PKG_CONFIG_LIBDIR}
      "${BHP_AARCH64_SYSROOT}/usr/lib/pkgconfig:${BHP_AARCH64_SYSROOT}/usr/share/pkgconfig")
else()
  set(CMAKE_FIND_ROOT_PATH "/nonexistent-bhp-aarch64-prefix")
  set(ENV{PKG_CONFIG_LIBDIR} "/nonexistent-bhp-aarch64-prefix")
endif()
