cmake_minimum_required(VERSION 3.21)
if(NOT ARK_TEST_DATABASE_EXECUTABLE OR NOT ARK_TEST_DATABASE_PLUGIN_DIR)
    message(FATAL_ERROR "Set the installed database consumer executable and plugin directory")
endif()
# This is a test-process guard, separate from the consumer's cooperative
# five-second resource observation and every production shutdown setting.
execute_process(COMMAND "${ARK_TEST_DATABASE_EXECUTABLE}" "${ARK_TEST_DATABASE_PLUGIN_DIR}"
    TIMEOUT 15 RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Installed database consumer failed (15-second process limit): ${result}")
endif()
