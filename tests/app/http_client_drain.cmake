# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
file(MAKE_DIRECTORY "${RUNTIME_DIR}")
execute_process(COMMAND "${PROGRAM}" "${PLUGIN_DIR}" WORKING_DIRECTORY "${RUNTIME_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 20)
file(WRITE "${RUNTIME_DIR}/output.log" "${output}\n${errors}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "HTTP actual DSO drain regression failed: ${result}\n${output}\n${errors}")
endif()
message(STATUS "${output}")
