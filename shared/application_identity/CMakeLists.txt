set(BHP_APPLIANCE_RELEASE_ID "" CACHE STRING
  "Pre-derived appliance release identity for source archives/cross-builds")

if(BHP_APPLIANCE_RELEASE_ID STREQUAL "")
  execute_process(
    COMMAND "${PROJECT_SOURCE_DIR}/scripts/build/application_release_id.sh"
      "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE BHP_APPLICATION_RELEASE_ID_STATUS
    OUTPUT_VARIABLE BHP_DERIVED_APPLICATION_RELEASE_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT BHP_APPLICATION_RELEASE_ID_STATUS EQUAL 0)
    message(FATAL_ERROR
      "BHP_APPLIANCE_RELEASE_ID is required when canonical derivation is unavailable")
  endif()
  set(BHP_APPLIANCE_RELEASE_ID "${BHP_DERIVED_APPLICATION_RELEASE_ID}")
endif()

string(LENGTH "${BHP_APPLIANCE_RELEASE_ID}" BHP_APPLIANCE_RELEASE_ID_LENGTH)
if(BHP_APPLIANCE_RELEASE_ID_LENGTH GREATER 128 OR
   NOT BHP_APPLIANCE_RELEASE_ID MATCHES "^[A-Za-z0-9][A-Za-z0-9._+-]*$")
  message(FATAL_ERROR
    "BHP_APPLIANCE_RELEASE_ID must be 1..128 bytes in the protocol-safe ASCII grammar")
endif()

add_library(bhp_application_identity INTERFACE)
add_library(bhp::application_identity ALIAS bhp_application_identity)
target_include_directories(bhp_application_identity INTERFACE include)
target_compile_features(bhp_application_identity INTERFACE cxx_std_17)
target_compile_definitions(bhp_application_identity INTERFACE
  BHP_APPLIANCE_RELEASE_ID="${BHP_APPLIANCE_RELEASE_ID}")

message(STATUS "BHP appliance release identity: ${BHP_APPLIANCE_RELEASE_ID}")
