# Bound only our regression children, never production cleanup.
execute_process(COMMAND "${PROGRAM}" normal TIMEOUT 20
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0" OR NOT errors MATCHES "Net listener normal controls passed")
    message(FATAL_ERROR "Net listener normal controls failed: ${result}")
endif()
foreach(mode exception retry)
    foreach(protocol tcp rudp)
        execute_process(COMMAND "${PROGRAM}" "${mode}" "${protocol}" TIMEOUT 10
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
        message("${output}${errors}")
        if(NOT "${result}" STREQUAL "1"
            OR NOT errors MATCHES "Net listener ownership: complete unpublished owner and worker retained; runner exit 1"
            OR NOT errors MATCHES "Application startup failed"
            OR errors MATCHES "forbidden Net listener|unexpected Net listener")
            message(FATAL_ERROR "Net listener ${protocol} exception ownership failed: ${result}")
        endif()
    endforeach()
endforeach()
