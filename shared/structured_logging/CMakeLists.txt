add_library(bhp_structured_logging STATIC
  src/structured_logger.cpp
)

target_include_directories(bhp_structured_logging PUBLIC include)
target_compile_features(bhp_structured_logging PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bhp_structured_logging PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BHP_BUILD_TESTS)
  add_executable(structured_logger_test tests/structured_logger_test.cpp)
  target_link_libraries(structured_logger_test PRIVATE bhp_structured_logging)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(structured_logger_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME structured_logger_test COMMAND structured_logger_test)
endif()
