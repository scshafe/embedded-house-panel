find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# @canonical media_capture_library -- capture sources, continuity and the take store.
add_library(bhp_media_capture STATIC
  src/capture_source.cpp
  src/continuity_log.cpp
  src/take_store.cpp
)
add_library(bhp::media_capture ALIAS bhp_media_capture)
target_include_directories(bhp_media_capture PUBLIC include)
target_compile_features(bhp_media_capture PUBLIC cxx_std_17)
target_link_libraries(bhp_media_capture PUBLIC nlohmann_json::nlohmann_json PRIVATE OpenSSL::Crypto)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp_media_capture PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(media_capture_test tests/media_capture_test.cpp)
  target_link_libraries(media_capture_test PRIVATE bhp_media_capture OpenSSL::Crypto)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(media_capture_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME media_capture_test COMMAND media_capture_test)
endif()
