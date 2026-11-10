# @canonical weather_panel_build -- the daily forecast domain (ADR 0015).
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json REQUIRED)
endif()
find_package(Threads REQUIRED)
add_library(weather_panel_core STATIC
  src/fixture_weather_client.cpp
  src/weather_codec.cpp
  src/weather_schedule.cpp
  src/weather_store.cpp
  src/weather_worker.cpp
)
target_include_directories(weather_panel_core PUBLIC include "${PROJECT_SOURCE_DIR}/shared/http_runtime/include")
target_compile_features(weather_panel_core PUBLIC cxx_std_17)
target_link_libraries(weather_panel_core PUBLIC bhp_structured_logging Threads::Threads
  PRIVATE nlohmann_json::nlohmann_json bhp::durable_store)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(weather_panel_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_HTTP_LEXINET_CLIENT)
  add_library(weather_panel_http STATIC src/http_weather_client.cpp)
  target_link_libraries(weather_panel_http PUBLIC weather_panel_core bhp_http_runtime)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(weather_panel_http PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

if(BHP_BUILD_TESTS)
  foreach(test_name codec store schedule worker)
    add_executable(weather_panel_${test_name}_test tests/weather_${test_name}_test.cpp)
    target_link_libraries(weather_panel_${test_name}_test PRIVATE weather_panel_core nlohmann_json::nlohmann_json)
    target_compile_definitions(weather_panel_${test_name}_test
      PRIVATE WEATHER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(weather_panel_${test_name}_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME weather_panel_${test_name}_test COMMAND weather_panel_${test_name}_test)
  endforeach()
  if(BHP_BUILD_HTTP_LEXINET_CLIENT)
    find_package(OpenSSL REQUIRED)
    # The vocabulary client's loopback TLS fixture: one canned answer per server, no internet.
    add_executable(weather_panel_http_client_test
      tests/http_weather_client_test.cpp
      "${PROJECT_SOURCE_DIR}/vocabulary_panel/tests/fixtures/loopback_http_server.cpp")
    target_include_directories(weather_panel_http_client_test PRIVATE "${PROJECT_SOURCE_DIR}/vocabulary_panel/tests")
    target_link_libraries(weather_panel_http_client_test
      PRIVATE weather_panel_http OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    target_compile_definitions(weather_panel_http_client_test
      PRIVATE WEATHER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(weather_panel_http_client_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME weather_panel_http_client_test COMMAND weather_panel_http_client_test)
  endif()
endif()
