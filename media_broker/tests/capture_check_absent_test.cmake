# A capture device that does not exist is a structured failure, not a crash,
# and leaves no take behind.
file(REMOVE_RECURSE "${ROOT}")
execute_process(COMMAND "${TOOL}" --pcm hw:CARD=BhpNoSuchCard,DEV=0 --seconds 1 --take-root "${ROOT}"
  --no-device-control RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_QUIET)
if(NOT status EQUAL 1)
  message(FATAL_ERROR "expected exit 1, got ${status}: ${output}")
endif()
string(JSON code GET "${output}" error code)
if(NOT code MATCHES "^MEDIA_CAPTURE\\.(DEVICE_ABSENT|OPEN_FAILED)$")
  message(FATAL_ERROR "unexpected error: ${output}")
endif()
file(GLOB left "${ROOT}/*")
if(left)
  message(FATAL_ERROR "files left behind: ${left}")
endif()
file(REMOVE_RECURSE "${ROOT}")
