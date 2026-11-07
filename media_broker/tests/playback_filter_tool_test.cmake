# The playback filter passes whole frames through at the same length, removes a
# constant offset, and refuses input that ends inside a frame or an unknown speaker.
file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
# 4000 stereo frames of the constant sample 0x4141.
string(REPEAT "AAAA" 4000 frames)
file(WRITE "${ROOT}/in.raw" "${frames}")
execute_process(COMMAND "${TOOL}" INPUT_FILE "${ROOT}/in.raw" OUTPUT_FILE "${ROOT}/out.raw"
  RESULT_VARIABLE status ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "expected exit 0, got ${status}: ${errors}")
endif()
file(SIZE "${ROOT}/out.raw" size)
if(NOT size EQUAL 16000)
  message(FATAL_ERROR "expected 16000 bytes, got ${size}")
endif()
file(READ "${ROOT}/out.raw" tail OFFSET 15992 HEX)
if(NOT tail STREQUAL "0000000000000000")
  message(FATAL_ERROR "the offset is not removed: ${tail}")
endif()

file(WRITE "${ROOT}/partial.raw" "AAAAAA")
execute_process(COMMAND "${TOOL}" INPUT_FILE "${ROOT}/partial.raw" OUTPUT_QUIET
  RESULT_VARIABLE status ERROR_VARIABLE errors)
string(JSON code ERROR_VARIABLE json_error GET "${errors}" error code)
if(NOT status EQUAL 1 OR NOT code STREQUAL "MEDIA_PLAYBACK.PARTIAL_FRAME")
  message(FATAL_ERROR "a partial frame: exit ${status}, ${errors}")
endif()

execute_process(COMMAND "${TOOL}" --speaker none INPUT_FILE "${ROOT}/in.raw" OUTPUT_QUIET
  RESULT_VARIABLE status ERROR_VARIABLE errors)
string(JSON code ERROR_VARIABLE json_error GET "${errors}" error code)
if(NOT status EQUAL 1 OR NOT code STREQUAL "MEDIA_PLAYBACK.SPEAKER_UNKNOWN")
  message(FATAL_ERROR "an unknown speaker: exit ${status}, ${errors}")
endif()
file(REMOVE_RECURSE "${ROOT}")
