if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(beaglebone_house_panel_panel_ui VERSION 0.1.0 LANGUAGES CXX)
  include(CTest)
  option(PANEL_UI_BUILD_TESTS "Build panel_ui host tests" ON)
elseif(NOT DEFINED PANEL_UI_BUILD_TESTS)
  if(DEFINED BHP_BUILD_TESTS)
    set(PANEL_UI_BUILD_TESTS ${BHP_BUILD_TESTS})
  else()
    set(PANEL_UI_BUILD_TESTS OFF)
  endif()
endif()

include(GNUInstallDirs)

if(NOT TARGET bhp_application_identity)
  set(PANEL_UI_SAVED_PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
  set(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../shared/application_identity"
    "${CMAKE_CURRENT_BINARY_DIR}/shared/application_identity"
  )
  set(PROJECT_SOURCE_DIR "${PANEL_UI_SAVED_PROJECT_SOURCE_DIR}")
  unset(PANEL_UI_SAVED_PROJECT_SOURCE_DIR)
endif()
if(NOT TARGET bhp_panel_mode_protocol)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../shared/panel_mode_protocol"
    "${CMAKE_CURRENT_BINARY_DIR}/shared/panel_mode_protocol"
  )
endif()
if(NOT TARGET bhp_touch_observation_protocol)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../shared/touch_observation_protocol"
    "${CMAKE_CURRENT_BINARY_DIR}/shared/touch_observation_protocol"
  )
endif()

add_library(panel_ui_core STATIC
  src/bounded_unicode_text_policy.cpp
  src/fail_dark_lifecycle_display_commit_backend.cpp
  src/headless_renderer.cpp
  src/interaction_intent.cpp
  src/panel_intent_registry.cpp
  src/panel_ui_error.cpp
  src/panel_ui_service.cpp
  src/shell_controller.cpp
  src/shell_state.cpp
  src/unix_socket_lifecycle_client.cpp
)
add_library(bhp::panel_ui_core ALIAS panel_ui_core)
target_include_directories(panel_ui_core PUBLIC include)
target_link_libraries(panel_ui_core PUBLIC
  bhp::application_identity
  bhp::panel_mode_protocol
  bhp::touch_observation_protocol
)
target_compile_features(panel_ui_core PUBLIC cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(panel_ui_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(bhp-panel-ui src/panel_ui_main.cpp)
target_link_libraries(bhp-panel-ui PRIVATE panel_ui_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp-panel-ui PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
install(TARGETS bhp-panel-ui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(PANEL_UI_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(panel_ui_core_smoke src/panel_ui_core_smoke.cpp)
  target_link_libraries(panel_ui_core_smoke PRIVATE panel_ui_core)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(panel_ui_core_smoke PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME panel_ui_core_smoke COMMAND panel_ui_core_smoke)
  foreach(test_name
      panel_intent_registry
      shell_controller
      bounded_unicode_text_policy
      panel_ui_service
      headless_renderer
      unix_socket_lifecycle_client)
    add_executable(panel_ui_${test_name}_test tests/${test_name}_test.cpp)
    target_link_libraries(panel_ui_${test_name}_test PRIVATE panel_ui_core Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(panel_ui_${test_name}_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME panel_ui_${test_name}_test COMMAND panel_ui_${test_name}_test)
  endforeach()
  # CTest versions disagree on how WILL_FAIL combines with
  # PASS_REGULAR_EXPRESSION, so assert the nonzero exit and the stable error code
  # explicitly instead of relying on either property.
  add_test(NAME panel_ui_production_client_requires_explicit_configuration
    COMMAND sh -c "output=$(\"$0\" 2>&1); test \"$?\" -eq 2 && printf '%s\\n' \"$output\" | grep -F PANEL_UI.LIFECYCLE_SOCKET_CONFIGURATION_INVALID"
      $<TARGET_FILE:bhp-panel-ui>)
endif()

if(BHP_BUILD_PANEL_RENDERER OR BHP_BUILD_PANEL_SIMULATOR OR BHP_BUILD_PANEL_DRM)
  find_package(PNG REQUIRED)
  add_library(panel_ui_lvgl STATIC src/lvgl_panel_display.cpp)
  target_include_directories(panel_ui_lvgl PUBLIC include)
  target_link_libraries(panel_ui_lvgl PUBLIC panel_ui_core vocabulary_panel_lvgl_renderer bhp::panel_fonts lvgl::lvgl PNG::PNG)
endif()
