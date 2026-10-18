# Clang cross toolchain for programs that run on the FRDM board's NXP vendor
# image (6.18-wrynose, glibc 2.43), as in the panel UI demo plan
# (.claude/plans/2026-10-18-frdm-panel-ui-demo.md). It is not the project image
# toolchain (buildroot-aarch64.cmake) and builds no image artifacts.
#
# Sysroot: scripts/build/frdm_vendor_sysroot.sh --execute (Debian trixie arm64,
# glibc 2.41). libstdc++ and libgcc are linked statically, so the binaries need
# only the board's libc, libm, libdrm, libpng16, libz and libcrypto.
# Environment: BHP_FRDM_VENDOR_SYSROOT overrides the default .build/frdm-sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{BHP_FRDM_VENDOR_SYSROOT} AND NOT "$ENV{BHP_FRDM_VENDOR_SYSROOT}" STREQUAL "")
  set(BHP_FRDM_VENDOR_SYSROOT "$ENV{BHP_FRDM_VENDOR_SYSROOT}")
else()
  get_filename_component(BHP_FRDM_VENDOR_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../../.build/frdm-sysroot" ABSOLUTE)
endif()
if(NOT EXISTS "${BHP_FRDM_VENDOR_SYSROOT}/usr/lib/aarch64-linux-gnu/libc.so.6")
  message(FATAL_ERROR "No FRDM vendor sysroot at ${BHP_FRDM_VENDOR_SYSROOT}; run scripts/build/frdm_vendor_sysroot.sh --execute")
endif()

find_program(BHP_CLANG clang NO_CMAKE_FIND_ROOT_PATH)
find_program(BHP_CLANGXX clang++ NO_CMAKE_FIND_ROOT_PATH)
find_program(BHP_LLD ld.lld NO_CMAKE_FIND_ROOT_PATH)
if(NOT BHP_CLANG OR NOT BHP_CLANGXX OR NOT BHP_LLD)
  message(FATAL_ERROR "clang, clang++ and ld.lld are required on PATH")
endif()
set(CMAKE_C_COMPILER "${BHP_CLANG}")
set(CMAKE_CXX_COMPILER "${BHP_CLANGXX}")
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
# LVGL builds .S sources; without this they would be assembled for the host.
set(CMAKE_ASM_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT "${BHP_FRDM_VENDOR_SYSROOT}")
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a53")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-a53")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libstdc++ -static-libgcc")

set(CMAKE_FIND_ROOT_PATH "${BHP_FRDM_VENDOR_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${BHP_FRDM_VENDOR_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${BHP_FRDM_VENDOR_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${BHP_FRDM_VENDOR_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
