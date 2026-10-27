# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
file(MAKE_DIRECTORY "${RUN_DIR}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef token)
set(output "${RUN_DIR}/${token} output")
execute_process(COMMAND "${TOOL}" --schema "${FIXTURES}/fields.csv"
    --data "GuestPlayer=${FIXTURES}/GuestPlayer.csv" --output "${output}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 10)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native export failed: ${result}: ${stderr}")
endif()
execute_process(COMMAND "${TOOL}" --validate "${output}/meta/entity_schema.json"
    RESULT_VARIABLE result ERROR_VARIABLE stderr TIMEOUT 10)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Export did not pass shared runtime validation: ${stderr}")
endif()
file(SHA256 "${output}/meta/entity_schema.json" before)
execute_process(COMMAND "${TOOL}" --schema "${FIXTURES}/fields.csv"
    --data "GuestPlayer=${FIXTURES}/GuestPlayer.csv" --output "${output}"
    RESULT_VARIABLE result ERROR_VARIABLE stderr TIMEOUT 10)
if(result EQUAL 0)
    message(FATAL_ERROR "Existing bundle was overwritten without explicit --replace")
endif()
file(SHA256 "${output}/meta/entity_schema.json" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "Failed export modified the previous valid output")
endif()
execute_process(COMMAND "${TOOL}" --unknown-option RESULT_VARIABLE result ERROR_VARIABLE stderr TIMEOUT 10)
if(NOT result EQUAL 2)
    message(FATAL_ERROR "Usage errors must return exit code 2")
endif()
message(STATUS "Native configuration CLI export, shared validation, overwrite protection and exit codes passed")
