# @canonical voice_lab_recorder_library -- the voice-lab recorder's sign-in and token store.
add_library(voice_lab_recorder_sign_in STATIC src/pocket_id_sign_in.cpp)
target_include_directories(voice_lab_recorder_sign_in PUBLIC include)
target_compile_features(voice_lab_recorder_sign_in PUBLIC cxx_std_17)
target_link_libraries(voice_lab_recorder_sign_in
  PUBLIC bhp_http_runtime bhp::durable_store PRIVATE nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(voice_lab_recorder_sign_in PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# @canonical voice_lab_recorder_take_package_library -- canonical JSON and the corpus-take/v2 packager.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
add_library(voice_lab_recorder_take_package STATIC src/canonical_json.cpp src/take_package.cpp)
target_include_directories(voice_lab_recorder_take_package PUBLIC include)
target_compile_features(voice_lab_recorder_take_package PUBLIC cxx_std_17)
target_link_libraries(voice_lab_recorder_take_package
  PUBLIC nlohmann_json::nlohmann_json PRIVATE OpenSSL::Crypto)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(voice_lab_recorder_take_package PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(voice_lab_canonical_json_take_package_test tests/take_package_test.cpp)
  target_link_libraries(voice_lab_canonical_json_take_package_test PRIVATE voice_lab_recorder_take_package)
  target_compile_definitions(voice_lab_canonical_json_take_package_test
    PRIVATE BHP_VOICE_LAB_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/corpus_client_v1")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(voice_lab_canonical_json_take_package_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME voice_lab_canonical_json_take_package_test COMMAND voice_lab_canonical_json_take_package_test)
  add_executable(voice_lab_device_code_token_store_test tests/pocket_id_sign_in_test.cpp)
  target_link_libraries(voice_lab_device_code_token_store_test PRIVATE voice_lab_recorder_sign_in)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(voice_lab_device_code_token_store_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME voice_lab_device_code_token_store_test COMMAND voice_lab_device_code_token_store_test)
  # Live corpus-client.v1 client. `make verify-voice-lab-live` runs it against
  # voice-lab's fixture server; it is never part of default CTest.
  add_executable(voice_lab_recorder_live_contract_client tools/live_contract_client.cpp)
  target_link_libraries(voice_lab_recorder_live_contract_client
    PRIVATE voice_lab_recorder_sign_in nlohmann_json::nlohmann_json)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(voice_lab_recorder_live_contract_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME voice_lab_corpus_client_v1_contract_fixture_test
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus_client_v1_contract_fixture_test.py")
endif()
