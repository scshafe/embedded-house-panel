find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# @canonical media_capture_library -- capture sources, continuity and the take store.
add_library(bhp_media_capture STATIC
  src/capture_source.cpp
  src/continuity_log.cpp
  src/take_store.cpp
  src/xvf3800_control.cpp
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

if(BHP_BUILD_MEDIA_ALSA)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
  add_library(bhp_media_alsa STATIC src/alsa_capture_source.cpp)
  add_library(bhp::media_alsa ALIAS bhp_media_alsa)
  target_link_libraries(bhp_media_alsa PUBLIC bhp_media_capture PRIVATE PkgConfig::ALSA)
  add_executable(bhp-media-capture-check tools/capture_check_main.cpp)
  target_link_libraries(bhp-media-capture-check PRIVATE bhp_media_alsa)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bhp_media_alsa PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(bhp-media-capture-check PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(BHP_BUILD_TESTS)
    add_test(NAME media_capture_check_fake_test
      COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:bhp-media-capture-check>
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/capture-check-takes
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_check_fake_test.cmake)
    add_test(NAME media_capture_alsa_absent_test
      COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:bhp-media-capture-check>
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/capture-check-absent
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_check_absent_test.cmake)
  endif()
endif()
