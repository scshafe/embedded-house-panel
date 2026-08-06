find_package(PkgConfig REQUIRED)
pkg_check_modules(BHP_LIBDRM REQUIRED IMPORTED_TARGET libdrm)

if(TARGET lvgl::lvgl)
  set(BHP_LVGL_LINK_TARGET lvgl::lvgl)
  set(BHP_LVGL_INSTALLED_CONFIGURATION OFF)
else()
  pkg_check_modules(BHP_LVGL REQUIRED IMPORTED_TARGET "lvgl=9.5.0")
  set(BHP_LVGL_LINK_TARGET PkgConfig::BHP_LVGL)
  set(BHP_LVGL_INSTALLED_CONFIGURATION ON)
endif()

add_library(vocabulary_panel_lvgl_renderer STATIC
  src/lvgl_flash_card_renderer.cpp
  src/lvgl_flash_card_renderer_constants.h
)
add_library(vocabulary_panel::lvgl_renderer ALIAS vocabulary_panel_lvgl_renderer)

target_include_directories(vocabulary_panel_lvgl_renderer PUBLIC include)
target_compile_features(vocabulary_panel_lvgl_renderer PUBLIC cxx_std_17)
target_link_libraries(vocabulary_panel_lvgl_renderer
  PUBLIC
    vocabulary_panel_core
    ${BHP_LVGL_LINK_TARGET}
    PkgConfig::BHP_LIBDRM
)

# LVGL 9.5's installed pkg-config file publishes its include directory but not
# the preprocessor switches required to consume the installed lv_conf.h.
# The in-tree CMake target already propagates its exact configuration path.
if(BHP_LVGL_INSTALLED_CONFIGURATION)
  target_compile_definitions(vocabulary_panel_lvgl_renderer
    PRIVATE LV_KCONFIG_IGNORE LV_CONF_INCLUDE_SIMPLE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(vocabulary_panel_lvgl_renderer
    PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(GNUInstallDirs)
install(TARGETS vocabulary_panel_lvgl_renderer
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
