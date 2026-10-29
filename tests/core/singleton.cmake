# The old primitive is a diagnostic control in its own process. A timeout or
# nonzero exit is reported, not accepted as evidence that the actual suite passed.
execute_process(COMMAND "${PROGRAM}" --probe-call-once
    RESULT_VARIABLE probe_result OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_errors TIMEOUT 5)
message(STATUS "call_once diagnostic control (5-second bound), result: ${probe_result}
${probe_output}
${probe_errors}")

# The actual suite is a separate, strict correctness gate. Preserve every error,
# sanitizer report and timeout; a hung assertion must fail this target promptly.
execute_process(COMMAND "${PROGRAM}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
message(STATUS "Singleton output:
${output}
${errors}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Singleton regression failed or exceeded the 30-second process bound: ${result}")
endif()
