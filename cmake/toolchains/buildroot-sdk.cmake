# @canonical buildroot_cross_toolchain -- all Buildroot-SDK application builds use this file.
# Board wrappers (buildroot-aarch64.cmake, deprecated buildroot-armv7.cmake) set
# BHP_BUILDROOT_TARGET_PROCESSOR from config/boards/<id>.env
# (BHP_BOARD_CMAKE_SYSTEM_PROCESSOR) before including this file.
if(NOT DEFINED BHP_BUILDROOT_TARGET_PROCESSOR)
  message(FATAL_ERROR "Include a board wrapper such as buildroot-aarch64.cmake; BHP_BUILDROOT_TARGET_PROCESSOR is unset")
endif()
if(NOT BHP_BUILDROOT_TARGET_PROCESSOR MATCHES "^(aarch64|arm)$")
  message(FATAL_ERROR "BHP_BUILDROOT_TARGET_PROCESSOR must be aarch64 or arm")
endif()
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "${BHP_BUILDROOT_TARGET_PROCESSOR}")

if(NOT DEFINED ENV{BHP_BUILDROOT_HOST_DIR})
  message(FATAL_ERROR "BHP_BUILDROOT_HOST_DIR must name Buildroot output/host")
endif()

if(NOT DEFINED ENV{BHP_BUILDROOT_CROSS_PREFIX})
  message(FATAL_ERROR "BHP_BUILDROOT_CROSS_PREFIX must be the full compiler prefix from config/boards/<id>.env, for example aarch64-buildroot-linux-gnu")
endif()

set(BHP_BUILDROOT_HOST_DIR "$ENV{BHP_BUILDROOT_HOST_DIR}")
set(BHP_BUILDROOT_CROSS_PREFIX "$ENV{BHP_BUILDROOT_CROSS_PREFIX}")

if(NOT IS_ABSOLUTE "${BHP_BUILDROOT_HOST_DIR}")
  message(FATAL_ERROR "BHP_BUILDROOT_HOST_DIR must be absolute")
endif()
if(NOT BHP_BUILDROOT_CROSS_PREFIX MATCHES "^[A-Za-z0-9._+-]+$")
  message(FATAL_ERROR "BHP_BUILDROOT_CROSS_PREFIX contains unsupported characters")
endif()
if(NOT BHP_BUILDROOT_CROSS_PREFIX MATCHES "^${BHP_BUILDROOT_TARGET_PROCESSOR}-")
  message(FATAL_ERROR "BHP_BUILDROOT_CROSS_PREFIX ${BHP_BUILDROOT_CROSS_PREFIX} does not target ${BHP_BUILDROOT_TARGET_PROCESSOR}")
endif()

set(BHP_BUILDROOT_TOOL_BIN_DIR "${BHP_BUILDROOT_HOST_DIR}/bin")
set(BHP_BUILDROOT_SYSROOT
    "${BHP_BUILDROOT_HOST_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}/sysroot")
set(BHP_BUILDROOT_PKG_CONFIG "${BHP_BUILDROOT_TOOL_BIN_DIR}/pkg-config")

foreach(required_tool gcc g++ ar ranlib strip)
  set(required_tool_path
      "${BHP_BUILDROOT_TOOL_BIN_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}-${required_tool}")
  if(NOT EXISTS "${required_tool_path}")
    message(FATAL_ERROR "Buildroot cross tool is absent: ${required_tool_path}")
  endif()
endforeach()
if(NOT EXISTS "${BHP_BUILDROOT_PKG_CONFIG}")
  message(FATAL_ERROR "Buildroot host pkg-config is absent: ${BHP_BUILDROOT_PKG_CONFIG}")
endif()
if(NOT IS_DIRECTORY "${BHP_BUILDROOT_SYSROOT}")
  message(FATAL_ERROR "Buildroot target sysroot is absent: ${BHP_BUILDROOT_SYSROOT}")
endif()

set(CMAKE_C_COMPILER
    "${BHP_BUILDROOT_TOOL_BIN_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}-gcc"
    CACHE FILEPATH "Buildroot target C compiler" FORCE)
set(CMAKE_CXX_COMPILER
    "${BHP_BUILDROOT_TOOL_BIN_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}-g++"
    CACHE FILEPATH "Buildroot target C++ compiler" FORCE)
set(CMAKE_AR
    "${BHP_BUILDROOT_TOOL_BIN_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}-ar"
    CACHE FILEPATH "Buildroot target archiver" FORCE)
set(CMAKE_RANLIB
    "${BHP_BUILDROOT_TOOL_BIN_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}-ranlib"
    CACHE FILEPATH "Buildroot target archive indexer" FORCE)
set(CMAKE_STRIP
    "${BHP_BUILDROOT_TOOL_BIN_DIR}/${BHP_BUILDROOT_CROSS_PREFIX}-strip"
    CACHE FILEPATH "Buildroot target strip tool" FORCE)
set(CMAKE_SYSROOT "${BHP_BUILDROOT_SYSROOT}"
    CACHE PATH "Buildroot target sysroot" FORCE)

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Never consult a developer machine's package registries or package hints for
# target dependencies. Host programs such as make remain discoverable, but
# every target header, library, and CMake package must resolve below SYSROOT.
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY TRUE)
set(CMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY TRUE)
set(CMAKE_PREFIX_PATH "${CMAKE_SYSROOT}/usr"
    CACHE STRING "Target-only CMake prefix" FORCE)
set(CMAKE_LIBRARY_PATH "" CACHE STRING "No host library hints" FORCE)
set(CMAKE_INCLUDE_PATH "" CACHE STRING "No host include hints" FORCE)
set(CMAKE_FRAMEWORK_PATH "" CACHE STRING "No host framework hints" FORCE)
set(CMAKE_APPBUNDLE_PATH "" CACHE STRING "No host app-bundle hints" FORCE)

# FindPkgConfig is a host-side CMake module, but its executable and search
# database must be the Buildroot SDK's target-aware wrapper/database.
set(PKG_CONFIG_EXECUTABLE "${BHP_BUILDROOT_PKG_CONFIG}"
    CACHE FILEPATH "Buildroot target pkg-config wrapper" FORCE)
set(ENV{PKG_CONFIG} "${BHP_BUILDROOT_PKG_CONFIG}")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig:${CMAKE_SYSROOT}/lib/pkgconfig")
