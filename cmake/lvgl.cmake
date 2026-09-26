# @canonical panel_lvgl_dependency -- one pinned LVGL for every display tier.
include(FetchContent)
enable_language(C)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/../panel_ui/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(bhp_lvgl
  URL https://codeload.github.com/lvgl/lvgl/tar.gz/refs/tags/v9.5.0
  URL_HASH SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(bhp_lvgl)
if(BHP_BUILD_PANEL_SIMULATOR)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(BHP_SDL2 REQUIRED IMPORTED_TARGET sdl2)
  target_compile_definitions(lvgl PUBLIC LV_USE_SDL=1)
  target_link_libraries(lvgl PUBLIC PkgConfig::BHP_SDL2)
endif()
