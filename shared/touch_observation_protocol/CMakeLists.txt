add_library(bhp_touch_observation_protocol STATIC
  src/bounded_normalized_touch_observation_stream.cpp
  src/touch_observation_validation.cpp
  src/touch_observation_codec.cpp
  src/unix_touch_observation_socket.cpp
)
add_library(bhp::touch_observation_protocol ALIAS bhp_touch_observation_protocol)

target_include_directories(bhp_touch_observation_protocol PUBLIC include)
target_compile_features(bhp_touch_observation_protocol PUBLIC cxx_std_17)
target_link_libraries(bhp_touch_observation_protocol PUBLIC bhp::panel_mode_protocol)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp_touch_observation_protocol PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(touch_observation_codec_test tests/touch_observation_codec_test.cpp)
  target_link_libraries(touch_observation_codec_test PRIVATE bhp_touch_observation_protocol)
  add_test(NAME touch_observation_codec_test COMMAND touch_observation_codec_test)
  add_executable(unix_touch_observation_socket_test tests/unix_touch_observation_socket_test.cpp)
  target_link_libraries(unix_touch_observation_socket_test PRIVATE bhp_touch_observation_protocol)
  add_test(NAME unix_touch_observation_socket_test COMMAND unix_touch_observation_socket_test)
  target_compile_definitions(bhp_touch_observation_protocol PUBLIC
    BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS=1)
  add_executable(touch_observation_protocol_test
    tests/bounded_normalized_touch_observation_stream_test.cpp)
  target_link_libraries(touch_observation_protocol_test PRIVATE
    bhp_touch_observation_protocol)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(touch_observation_protocol_test PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME touch_observation_protocol_test
    COMMAND touch_observation_protocol_test)
endif()
