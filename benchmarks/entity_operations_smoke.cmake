# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
foreach(argument PROGRAM PLUGIN_DIR SOURCE_ROOT RUNTIME_DIR)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${RUNTIME_DIR}/conf")
file(COPY "${SOURCE_ROOT}/examples/entity_operations/resource" DESTINATION "${RUNTIME_DIR}")
execute_process(COMMAND "${PROGRAM}" --plugin-dir "${PLUGIN_DIR}" --smoke
    WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 20)
file(WRITE "${RUNTIME_DIR}/smoke.log" "${output}\n${errors}")
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "Entity workload smoke passed: 8 iterations per loop; timing disabled")
    message(FATAL_ERROR "Entity workload smoke failed: ${result}\n${output}\n${errors}")
endif()
if(output MATCHES "query with \\[int key\\]|set with \\[int key\\]")
    message(FATAL_ERROR "Smoke must not report measured timings")
endif()
message(STATUS "Entity workload correctness smoke: eight iterations per loop, no timing measurement")

foreach(invalid 0 -1 invalid 184467440737095516160)
    execute_process(COMMAND "${PROGRAM}" --plugin-dir "${PLUGIN_DIR}" --iterations "${invalid}"
        WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE invalid_result
        OUTPUT_VARIABLE invalid_output ERROR_VARIABLE invalid_errors TIMEOUT 2)
    if(NOT "${invalid_result}" STREQUAL "2")
        message(FATAL_ERROR "Invalid iteration count must fail before starting plugins: ${invalid_result}")
    endif()
endforeach()
execute_process(COMMAND "${PROGRAM}" WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE no_args TIMEOUT 2
    OUTPUT_QUIET ERROR_QUIET)
if(NOT "${no_args}" STREQUAL "2")
    message(FATAL_ERROR "No arguments must not start a default measured run")
endif()
