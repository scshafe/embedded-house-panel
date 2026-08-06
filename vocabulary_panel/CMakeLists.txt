add_library(vocabulary_panel_core STATIC
  src/fixture_lexinet_client.cpp
  src/flash_card_session.cpp
)

target_include_directories(vocabulary_panel_core PUBLIC include)
target_compile_features(vocabulary_panel_core PUBLIC cxx_std_17)
target_link_libraries(vocabulary_panel_core PUBLIC bhp_structured_logging)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(vocabulary_panel_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(vocabulary_panel_terminal src/vocabulary_panel_terminal_main.cpp)
target_link_libraries(vocabulary_panel_terminal PRIVATE vocabulary_panel_core)

if(BHP_BUILD_HTTP_LEXINET_CLIENT)
  find_package(CURL REQUIRED)
  find_package(nlohmann_json REQUIRED)
  add_library(vocabulary_panel_http_lexinet STATIC src/http_lexinet_client.cpp)
  target_include_directories(vocabulary_panel_http_lexinet PUBLIC include)
  target_compile_features(vocabulary_panel_http_lexinet PUBLIC cxx_std_17)
  target_link_libraries(vocabulary_panel_http_lexinet
    PUBLIC vocabulary_panel_core CURL::libcurl nlohmann_json::nlohmann_json)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vocabulary_panel_http_lexinet PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

if(BHP_BUILD_LVGL_RENDERER)
  add_subdirectory(gui/lvgl)
endif()

install(TARGETS vocabulary_panel_terminal RUNTIME DESTINATION bin)

if(BHP_BUILD_TESTS)
  add_executable(vocabulary_panel_flash_card_session_test tests/flash_card_session_test.cpp)
  target_link_libraries(vocabulary_panel_flash_card_session_test PRIVATE vocabulary_panel_core)
  add_test(NAME vocabulary_panel_flash_card_session_test COMMAND vocabulary_panel_flash_card_session_test)
endif()
