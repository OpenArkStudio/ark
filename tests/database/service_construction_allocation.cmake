# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
set(protocols redis)
if(ENABLE_SSL)
    list(APPEND protocols redis-tls mysql)
endif()
foreach(protocol IN LISTS protocols)
    execute_process(COMMAND "${PROGRAM}" "${protocol}" -1
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL ""
        OR NOT output MATCHES "^constructed ${protocol} allocations=([0-9]+)\n$")
        message(FATAL_ERROR "Construction baseline ${protocol}: ${result}\n${output}${error}")
    endif()
    set(count "${CMAKE_MATCH_1}")
    if(count LESS 1 OR count GREATER 256)
        message(FATAL_ERROR "Unexpected construction allocation count: ${count}")
    endif()
    math(EXPR last "${count} - 1")
    foreach(budget RANGE 0 ${last})
        execute_process(COMMAND "${PROGRAM}" "${protocol}" "${budget}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
        if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL ""
            OR NOT "${output}" STREQUAL "caught ${protocol} budget=${budget} allocations=${budget}\n")
            message(FATAL_ERROR "Construction fault ${protocol} budget=${budget}: ${result}\n${output}${error}")
        endif()
    endforeach()
    execute_process(COMMAND "${PROGRAM}" "${protocol}" "${count}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL ""
        OR NOT "${output}" STREQUAL "constructed ${protocol} allocations=${count}\n")
        message(FATAL_ERROR "Construction success ${protocol}: ${result}\n${output}${error}")
    endif()
    message(STATUS "${PROFILE} ${protocol}: ${count} persistent allocation failures and 2 successful constructions")
endforeach()
