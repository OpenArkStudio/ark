# Bound only the owned regression process; expiry is a test failure.
execute_process(COMMAND "${PROGRAM}" TIMEOUT 20
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Net HTTP startup cleanup regressions failed: ${result}")
endif()

foreach(mode allocation-failure install-failure)
    if(mode STREQUAL "install-failure")
        set(arguments "--${mode}" "${FIXTURE_DIR}")
        set(expected "HTTP install failure: idle runtime joined once; retained DSO and owners; exit 1")
    else()
        set(arguments "--${mode}")
        set(expected "HTTP startup allocation failure: idle runtime joined; candidate gone; exit 1")
    endif()
    execute_process(COMMAND "${PROGRAM}" ${arguments} TIMEOUT 10
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    message("${output}${errors}")
    if(NOT "${result}" STREQUAL "1" OR NOT "${errors}" MATCHES "${expected}" OR
        "${errors}" MATCHES "forbidden HTTP fixture (uninstall|destruction)")
        message(FATAL_ERROR "Net HTTP startup ${mode} regression failed: ${result}")
    endif()
endforeach()
