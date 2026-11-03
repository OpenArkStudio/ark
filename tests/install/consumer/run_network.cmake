cmake_minimum_required(VERSION 3.21)
foreach(required EXECUTABLE PLUGIN_DIR BUS_CONFIG ENTITY_SCHEMA RUNTIME)
    if(NOT ARK_TEST_NETWORK_${required})
        message(FATAL_ERROR "Set ARK_TEST_NETWORK_${required}")
    endif()
endforeach()
# Fixture guard only; production settings and the cooperative five-second
# resource observation in the consumer remain unchanged.
execute_process(COMMAND "${ARK_TEST_NETWORK_EXECUTABLE}" "${ARK_TEST_NETWORK_PLUGIN_DIR}"
    "${ARK_TEST_NETWORK_RUNTIME}" "${ARK_TEST_NETWORK_BUS_CONFIG}" "${ARK_TEST_NETWORK_ENTITY_SCHEMA}"
    TIMEOUT 15 RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Installed network consumer failed (15-second process limit): ${result}")
endif()
