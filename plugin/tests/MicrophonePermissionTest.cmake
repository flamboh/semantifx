if(NOT DEFINED PLIST_PATH OR NOT EXISTS "${PLIST_PATH}")
  message(FATAL_ERROR "Standalone Info.plist not found: ${PLIST_PATH}")
endif()

execute_process(
  COMMAND /usr/bin/plutil -extract NSMicrophoneUsageDescription raw "${PLIST_PATH}"
  RESULT_VARIABLE permission_result
  OUTPUT_VARIABLE permission_text
  ERROR_VARIABLE permission_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT permission_result EQUAL 0 OR permission_text STREQUAL "")
  message(FATAL_ERROR
    "Standalone app cannot request microphone access: "
    "NSMicrophoneUsageDescription is missing from ${PLIST_PATH}. "
    "${permission_error}"
  )
endif()

message(STATUS "Microphone permission text: ${permission_text}")
