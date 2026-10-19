if(NOT TARGET nlohmann_json::nlohmann_json)
 find_package(nlohmann_json REQUIRED)
endif()
add_library(vocabulary_panel_core STATIC
  src/fixture_lexinet_client.cpp
  src/flash_card_session.cpp
  src/study_client_configuration.cpp
  src/study_models.cpp
  src/study_store.cpp
  src/study_wire_codec.cpp
)

target_include_directories(vocabulary_panel_core PUBLIC include "${PROJECT_SOURCE_DIR}/shared/http_runtime/include")
target_compile_features(vocabulary_panel_core PUBLIC cxx_std_17)
target_link_libraries(vocabulary_panel_core PUBLIC bhp_structured_logging nlohmann_json::nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(vocabulary_panel_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vocabulary_panel_terminal src/vocabulary_panel_terminal_main.cpp)
target_link_libraries(vocabulary_panel_terminal PRIVATE vocabulary_panel_core)

if(BHP_BUILD_TESTS)
  add_executable(vocabulary_panel_study_client_configuration_test
    tests/study_client_configuration_test.cpp)
  target_link_libraries(vocabulary_panel_study_client_configuration_test PRIVATE vocabulary_panel_core)
  target_compile_definitions(vocabulary_panel_study_client_configuration_test
    PRIVATE BHP_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vocabulary_panel_study_client_configuration_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME vocabulary_panel_study_client_configuration_test
    COMMAND vocabulary_panel_study_client_configuration_test)
endif()

if(BHP_BUILD_HTTP_LEXINET_CLIENT)
  find_package(CURL REQUIRED)
  add_library(vocabulary_panel_http_lexinet STATIC src/http_lexinet_client.cpp src/study_worker.cpp
    src/study_synchronizer.cpp)
  target_include_directories(vocabulary_panel_http_lexinet PUBLIC include)
  target_compile_features(vocabulary_panel_http_lexinet PUBLIC cxx_std_17)
  target_link_libraries(vocabulary_panel_http_lexinet
    PUBLIC vocabulary_panel_core bhp_http_runtime)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vocabulary_panel_http_lexinet PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(BHP_BUILD_TESTS)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    add_executable(vocabulary_panel_http_lexinet_client_test
      tests/fixtures/loopback_http_server.cpp
      tests/http_lexinet_client_test.cpp)
    target_include_directories(vocabulary_panel_http_lexinet_client_test PRIVATE tests)
    target_link_libraries(vocabulary_panel_http_lexinet_client_test
      PRIVATE
        vocabulary_panel_http_lexinet
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(vocabulary_panel_http_lexinet_client_test
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME vocabulary_panel_http_lexinet_client_test
      COMMAND vocabulary_panel_http_lexinet_client_test)
    add_executable(vocabulary_panel_study_worker_test tests/study_worker_test.cpp)
    target_link_libraries(vocabulary_panel_study_worker_test PRIVATE vocabulary_panel_http_lexinet)
    add_test(NAME vocabulary_panel_study_worker_test COMMAND vocabulary_panel_study_worker_test)
    add_executable(vocabulary_panel_study_synchronizer_test tests/study_synchronizer_test.cpp)
    target_link_libraries(vocabulary_panel_study_synchronizer_test PRIVATE vocabulary_panel_http_lexinet)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(vocabulary_panel_study_synchronizer_test
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME vocabulary_panel_study_synchronizer_test
      COMMAND vocabulary_panel_study_synchronizer_test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(vocabulary_panel_study_https_roundtrip_test tests/study_https_roundtrip_test.cpp)
    target_link_libraries(vocabulary_panel_study_https_roundtrip_test PRIVATE vocabulary_panel_http_lexinet)
    add_test(NAME vocabulary_panel_fixture_https_roundtrip_test
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture_study_server_test.py"
        --client "$<TARGET_FILE:vocabulary_panel_study_https_roundtrip_test>")
    # Live cross-repository study.v1 client. `make verify-lexinet-live` runs it
    # against a disposable real Lexinet; it is never part of default CTest.
    add_executable(vocabulary_panel_study_live_lexinet_contract_client
      tests/study_live_lexinet_contract_client.cpp)
    target_link_libraries(vocabulary_panel_study_live_lexinet_contract_client
      PRIVATE vocabulary_panel_http_lexinet OpenSSL::Crypto)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(vocabulary_panel_study_live_lexinet_contract_client
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endif()
endif()

if(BHP_BUILD_LVGL_RENDERER)
  add_subdirectory(gui/lvgl)
endif()

install(TARGETS vocabulary_panel_terminal RUNTIME DESTINATION bin)

if(BHP_BUILD_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(vocabulary_panel_flash_card_session_test tests/flash_card_session_test.cpp)
  target_link_libraries(vocabulary_panel_flash_card_session_test PRIVATE vocabulary_panel_core)
  add_test(NAME vocabulary_panel_flash_card_session_test COMMAND vocabulary_panel_flash_card_session_test)
  add_executable(vocabulary_panel_study_wire_codec_test tests/study_wire_codec_test.cpp)
  target_link_libraries(vocabulary_panel_study_wire_codec_test PRIVATE vocabulary_panel_core)
  add_test(NAME vocabulary_panel_study_wire_codec_test COMMAND vocabulary_panel_study_wire_codec_test)
  add_executable(vocabulary_panel_study_store_crash_test tests/study_store_crash_test.cpp)
  target_include_directories(vocabulary_panel_study_store_crash_test PRIVATE tests)
  target_link_libraries(vocabulary_panel_study_store_crash_test PRIVATE vocabulary_panel_core)
  add_test(NAME vocabulary_panel_study_store_crash_test COMMAND vocabulary_panel_study_store_crash_test)
  add_test(
    NAME vocabulary_panel_study_v1_contract_fixture_test
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/study_v1_contract_fixture_test.py")
endif()
