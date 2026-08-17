find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(bhp_panel_mode_protocol STATIC
  src/codec.cpp
  src/framing.cpp
)
add_library(bhp::panel_mode_protocol ALIAS bhp_panel_mode_protocol)

target_include_directories(bhp_panel_mode_protocol PUBLIC include)
target_compile_features(bhp_panel_mode_protocol PUBLIC cxx_std_17)
target_link_libraries(bhp_panel_mode_protocol PUBLIC OpenSSL::Crypto)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp_panel_mode_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(panel_mode_protocol_test tests/codec_test.cpp)
  target_link_libraries(panel_mode_protocol_test PRIVATE bhp_panel_mode_protocol)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(panel_mode_protocol_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME panel_mode_protocol_test COMMAND panel_mode_protocol_test)
endif()
