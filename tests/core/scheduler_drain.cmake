# Bound only this owned test process. Expiry is a failed observation, never a
# successful scheduler shutdown or permission to clean up production modules.
execute_process(COMMAND "${PROGRAM}" TIMEOUT 20
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Scheduler drain regressions failed: ${result}")
endif()
