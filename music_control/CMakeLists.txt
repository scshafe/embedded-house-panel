# @canonical music_control_protocol_library -- the UI's side of the music-control socket.
add_library(music_control_protocol STATIC src/music_protocol.cpp)
target_include_directories(music_control_protocol PUBLIC include)
target_compile_features(music_control_protocol PUBLIC cxx_std_17)
target_link_libraries(music_control_protocol
  PUBLIC bhp::media_window_protocol PRIVATE nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(music_control_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(music_control_protocol_test tests/music_protocol_test.cpp)
  target_link_libraries(music_control_protocol_test PRIVATE music_control_protocol)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(music_control_protocol_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME music_control_protocol_test COMMAND music_control_protocol_test)
endif()

# Everything below speaks HTTP: it shares the runtime the study client builds.
if(NOT BHP_BUILD_HTTP_LEXINET_CLIENT)
  return()
endif()

# @canonical music_control_library -- the Spotify sign-in, token store, Web API client and service (ADR 0013).
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
add_library(music_control STATIC src/spotify_auth.cpp src/spotify_client.cpp src/music_service.cpp)
target_include_directories(music_control PUBLIC include)
target_compile_features(music_control PUBLIC cxx_std_17)
target_link_libraries(music_control
  PUBLIC bhp_http_runtime bhp::durable_store music_control_protocol
  PRIVATE nlohmann_json::nlohmann_json OpenSSL::Crypto)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(music_control PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The service process: its own UID, the refresh token, commands to Spotify only.
add_executable(bhp-music-control tools/music_control_main.cpp)
target_link_libraries(bhp-music-control PRIVATE music_control bhp_structured_logging)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp-music-control PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The owner's one-time sign-in, run on the owner's PC (ADR 0013).
add_executable(bhp-music-spotify-sign-in tools/spotify_sign_in_main.cpp)
target_link_libraries(bhp-music-spotify-sign-in PRIVATE music_control nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp-music-spotify-sign-in PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_test(NAME music_control_daemon_usage_test COMMAND bhp-music-control --ui-socket /nonexistent)
  set_tests_properties(music_control_daemon_usage_test PROPERTIES WILL_FAIL TRUE)
  foreach(test_name spotify_auth spotify_client music_service)
    add_executable(music_control_${test_name}_test tests/${test_name}_test.cpp)
    target_link_libraries(music_control_${test_name}_test PRIVATE music_control nlohmann_json::nlohmann_json)
    target_compile_definitions(music_control_${test_name}_test
      PRIVATE BHP_SPOTIFY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/spotify")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(music_control_${test_name}_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME music_control_${test_name}_test COMMAND music_control_${test_name}_test)
  endforeach()
  # The sign-in tool end to end, with a loopback fake token endpoint and a fake browser.
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME music_control_sign_in_tool_test
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/spotify_sign_in_tool_test.py"
      "$<TARGET_FILE:bhp-music-spotify-sign-in>")
endif()
