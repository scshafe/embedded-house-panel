# @canonical bug_reporter_protocol_library -- the UI's side of the reporter socket.
add_library(bug_reporter_protocol STATIC src/reporter_protocol.cpp)
target_include_directories(bug_reporter_protocol PUBLIC include)
target_compile_features(bug_reporter_protocol PUBLIC cxx_std_17)
target_link_libraries(bug_reporter_protocol
  PUBLIC bhp::media_window_protocol nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bug_reporter_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(bug_reporter_protocol_test tests/reporter_protocol_test.cpp)
  target_link_libraries(bug_reporter_protocol_test PRIVATE bug_reporter_protocol)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bug_reporter_protocol_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME bug_reporter_protocol_test COMMAND bug_reporter_protocol_test)
endif()

# Everything below speaks HTTP: it shares the runtime the study client builds.
if(NOT BHP_BUILD_HTTP_LEXINET_CLIENT)
  return()
endif()

# @canonical bug_reporter_library -- rings, bundles, the outbox, the inbox client and the service.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
add_library(bug_reporter STATIC
  src/log_rings.cpp src/report_bundle.cpp src/description_audio.cpp src/report_outbox.cpp src/inbox_client.cpp src/ingest.cpp
  src/reporter_service.cpp)
target_include_directories(bug_reporter PUBLIC include)
target_compile_features(bug_reporter PUBLIC cxx_std_17)
target_link_libraries(bug_reporter
  PUBLIC bhp_http_runtime bhp::durable_store bug_reporter_protocol nlohmann_json::nlohmann_json
  PRIVATE OpenSSL::Crypto ZLIB::ZLIB)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bug_reporter PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The reporter process: its own UID, the outbox, the inbox over the tailnet.
add_executable(bhp-reporter tools/reporter_main.cpp)
target_link_libraries(bhp-reporter PRIVATE bug_reporter bhp_structured_logging)
# The tee each service's stderr runs through, and the restart wrapper's exit notice.
add_executable(bhp-log-tee tools/log_tee_main.cpp)
target_link_libraries(bhp-log-tee PRIVATE bug_reporter)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp-reporter PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(bhp-log-tee PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_test(NAME bug_reporter_daemon_usage_test COMMAND bhp-reporter --ui-socket /nonexistent)
  set_tests_properties(bug_reporter_daemon_usage_test PROPERTIES WILL_FAIL TRUE)
  foreach(test_name log_rings report_bundle report_outbox inbox_client reporter_service)
    add_executable(bug_reporter_${test_name}_test tests/${test_name}_test.cpp)
    target_link_libraries(bug_reporter_${test_name}_test PRIVATE bug_reporter ZLIB::ZLIB)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(bug_reporter_${test_name}_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME bug_reporter_${test_name}_test COMMAND bug_reporter_${test_name}_test)
  endforeach()
  # The daemon and the tee end to end, with a loopback fake inbox.
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME bug_reporter_daemon_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reporter_daemon_test.py"
      "$<TARGET_FILE:bhp-reporter>" "$<TARGET_FILE:bhp-log-tee>")
  # The same two against the PC's real receiver (report_inbox/), pinning the v1 API on both sides.
  add_test(NAME bug_reporter_inbox_contract_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reporter_inbox_contract_test.py"
      "$<TARGET_FILE:bhp-reporter>" "$<TARGET_FILE:bhp-log-tee>" "${CMAKE_SOURCE_DIR}/report_inbox")
endif()
