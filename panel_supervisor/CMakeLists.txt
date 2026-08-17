if(NOT TARGET bhp_structured_logging)
  message(FATAL_ERROR
    "panel_supervisor requires the canonical bhp_structured_logging target; configure from the repository root")
endif()
if(NOT TARGET bhp_panel_mode_protocol OR NOT TARGET bhp_application_identity OR
   NOT TARGET bhp_touch_observation_protocol)
  message(FATAL_ERROR
    "panel_supervisor requires canonical lifecycle, touch, and application identity targets")
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

add_library(panel_supervisor STATIC
  src/error_code.cpp
  src/panel_policy.cpp
  src/panel_supervisor.cpp
  src/presence_state.cpp
  src/solar_state.cpp
  src/supervisor_config.cpp
  src/supervisor_error.cpp
  src/supervisor_event.cpp
  src/system_state.cpp
  src/touch_observation_authority.cpp
  src/ui_mode.cpp
)

add_library(bhp::panel_supervisor ALIAS panel_supervisor)

target_include_directories(panel_supervisor PUBLIC include)
target_compile_features(panel_supervisor PUBLIC cxx_std_17)
target_link_libraries(panel_supervisor PUBLIC
  bhp_structured_logging
	bhp_application_identity
	bhp_panel_mode_protocol
  bhp::touch_observation_protocol
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(panel_supervisor PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(panel_supervisor_linux_adapters STATIC
  src/daemon_stop_controller.cpp
  src/evdev_presence_source.cpp
  src/exact_file_writer.cpp
  src/offline_solar_calculator.cpp
  src/panel_supervisord_config.cpp
  src/rate_limited_stop_aware_suspend.cpp
  src/suspend_mode.cpp
  src/sysfs_backlight.cpp
  src/sysfs_suspend.cpp
  src/system_clock.cpp
  src/unix_socket_ui_mode_notifier.cpp
)
add_library(bhp::panel_supervisor_linux_adapters ALIAS panel_supervisor_linux_adapters)
target_include_directories(panel_supervisor_linux_adapters
  PUBLIC include
  PRIVATE src
)
target_compile_features(panel_supervisor_linux_adapters PUBLIC cxx_std_17)
target_link_libraries(panel_supervisor_linux_adapters
  PUBLIC panel_supervisor bhp_panel_mode_protocol bhp_application_identity Threads::Threads
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(panel_supervisor_linux_adapters PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(panel_supervisord
  daemon/panel_supervisord.cpp
)
target_link_libraries(panel_supervisord PRIVATE panel_supervisor_linux_adapters Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(panel_supervisord PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
install(TARGETS panel_supervisord RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

add_executable(panel_supervisor_simulation
  simulation/panel_supervisor_simulation.cpp
)
target_link_libraries(panel_supervisor_simulation PRIVATE panel_supervisor)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(panel_supervisor_simulation PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(panel_policy_test tests/panel_policy_test.cpp)
  target_link_libraries(panel_policy_test PRIVATE panel_supervisor)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(panel_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME panel_policy_test COMMAND panel_policy_test)

  add_executable(panel_supervisor_test tests/panel_supervisor_test.cpp)
  target_link_libraries(panel_supervisor_test PRIVATE panel_supervisor)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(panel_supervisor_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME panel_supervisor_test COMMAND panel_supervisor_test)

  add_executable(supervisor_config_test tests/supervisor_config_test.cpp)
  target_link_libraries(supervisor_config_test PRIVATE panel_supervisor)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(supervisor_config_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME supervisor_config_test COMMAND supervisor_config_test)

  add_executable(touch_observation_authority_test
    tests/touch_observation_authority_test.cpp
  )
  target_link_libraries(touch_observation_authority_test
    PRIVATE panel_supervisor Threads::Threads
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(touch_observation_authority_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME touch_observation_authority_test COMMAND touch_observation_authority_test)

  add_executable(offline_solar_calculator_test tests/offline_solar_calculator_test.cpp)
  target_link_libraries(offline_solar_calculator_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(offline_solar_calculator_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME offline_solar_calculator_test COMMAND offline_solar_calculator_test)

  add_executable(panel_supervisord_config_test tests/panel_supervisord_config_test.cpp)
  target_link_libraries(panel_supervisord_config_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(panel_supervisord_config_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME panel_supervisord_config_test COMMAND panel_supervisord_config_test)

  add_executable(sysfs_backlight_test tests/sysfs_backlight_test.cpp)
  target_include_directories(sysfs_backlight_test PRIVATE src)
  target_link_libraries(sysfs_backlight_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(sysfs_backlight_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME sysfs_backlight_test COMMAND sysfs_backlight_test)

  add_executable(sysfs_suspend_test tests/sysfs_suspend_test.cpp)
  target_include_directories(sysfs_suspend_test PRIVATE src)
  target_link_libraries(sysfs_suspend_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(sysfs_suspend_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME sysfs_suspend_test COMMAND sysfs_suspend_test)

  add_executable(unix_socket_ui_mode_notifier_test
    tests/unix_socket_ui_mode_notifier_test.cpp
  )
  target_link_libraries(unix_socket_ui_mode_notifier_test
    PRIVATE panel_supervisor_linux_adapters
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(unix_socket_ui_mode_notifier_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME unix_socket_ui_mode_notifier_test COMMAND unix_socket_ui_mode_notifier_test)

  add_executable(evdev_presence_source_test tests/evdev_presence_source_test.cpp)
  target_link_libraries(evdev_presence_source_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(evdev_presence_source_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME evdev_presence_source_test COMMAND evdev_presence_source_test)

  add_executable(system_clock_test tests/system_clock_test.cpp)
  target_include_directories(system_clock_test PRIVATE src)
  target_link_libraries(system_clock_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(system_clock_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME system_clock_test COMMAND system_clock_test)

  add_executable(daemon_stop_controller_test tests/daemon_stop_controller_test.cpp)
  target_link_libraries(daemon_stop_controller_test PRIVATE panel_supervisor_linux_adapters)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(daemon_stop_controller_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME daemon_stop_controller_test COMMAND daemon_stop_controller_test)

  add_executable(rate_limited_stop_aware_suspend_test
    tests/rate_limited_stop_aware_suspend_test.cpp
  )
  target_link_libraries(rate_limited_stop_aware_suspend_test
    PRIVATE panel_supervisor_linux_adapters
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(rate_limited_stop_aware_suspend_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME rate_limited_stop_aware_suspend_test
    COMMAND rate_limited_stop_aware_suspend_test
  )
endif()
