# Runs bhp-media-capture-check on the paced fake source for one second and
# checks the JSON summary names a committed take with no xruns.
file(REMOVE_RECURSE "${ROOT}")
execute_process(COMMAND "${TOOL}" --pcm fake --seconds 1 --take-root "${ROOT}" --fake
  RESULT_VARIABLE status OUTPUT_VARIABLE output)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "capture check failed: ${status} ${output}")
endif()
string(JSON take_id GET "${output}" take_id)
string(JSON xruns GET "${output}" xruns)
string(JSON frames GET "${output}" frames)
string(JSON reason GET "${output}" stop_reason)
if(NOT xruns EQUAL 0 OR NOT frames EQUAL 15360 OR NOT reason STREQUAL "maximum-duration"
   OR NOT EXISTS "${ROOT}/${take_id}/capture.json" OR NOT EXISTS "${ROOT}/${take_id}/original.wav")
  message(FATAL_ERROR "unexpected summary: ${output}")
endif()
file(REMOVE_RECURSE "${ROOT}")
