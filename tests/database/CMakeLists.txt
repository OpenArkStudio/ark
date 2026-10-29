# Development-only database checks; no runtime application plugin is enabled here.
find_package(Threads REQUIRED)
if(NOT EXISTS "${ARKNET_SOURCE_DIR}/include/arknet/redis/pool.hpp")
    message(FATAL_ERROR "Database development tests require a local arknet checkout with the native Redis pool")
endif()

function(ark_database_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE "${ROOT_DIR}/src/plugins")
    target_link_libraries(${target} PRIVATE Threads::Threads)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
endfunction()

ark_database_test(ark_database_queue_tests main.cpp completion_queue.cpp request_control.cpp)
if(NOT EXISTS "${ARKNET_SOURCE_DIR}/include/arknet/base/manual_runtime.hpp")
    message(FATAL_ERROR "Database coroutine development tests require arknet manual_runtime")
endif()
ark_database_test(ark_database_task_tests main.cpp task_group.cpp awaitable_lifetime.cpp)
target_link_libraries(ark_database_task_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_task_contract_tests task_group_contract.cpp)
target_link_libraries(ark_database_task_contract_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_service_awaitable_tests service_awaitable.cpp)
target_link_libraries(ark_database_service_awaitable_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_operations_tests main.cpp operations_awaitable.cpp)
target_link_libraries(ark_database_operations_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_service_retirement_tests main.cpp service_retirement.cpp)
target_link_libraries(ark_database_service_retirement_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_redis_tests redis_service.cpp)
target_link_libraries(ark_database_redis_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_redis_stop_tests redis_stop_capture.cpp)
target_link_libraries(ark_database_redis_stop_tests PRIVATE arknet::arknet)
ark_database_test(ark_database_public_redis_tests main.cpp public_redis_module.cpp
    "${ROOT_DIR}/src/plugins/redis/redis_module.cpp")
target_link_libraries(ark_database_public_redis_tests PRIVATE ark::redis arknet::arknet)
# Real-service programs are compiled by check-database, but never run there.
ark_database_test(ark_database_public_service_tests public_service.cpp
    "${ROOT_DIR}/src/plugins/redis/redis_module.cpp")
target_link_libraries(ark_database_public_service_tests PRIVATE ark::redis arknet::arknet)
ark_database_test(ark_database_public_failure_service_tests public_failure_service.cpp
    "${ROOT_DIR}/src/plugins/redis/redis_module.cpp")
target_link_libraries(ark_database_public_failure_service_tests PRIVATE ark::redis arknet::arknet)
add_library(ark_database_redis_plugin SHARED
    "${ROOT_DIR}/src/plugins/redis/redis_plugin.cpp" "${ROOT_DIR}/src/plugins/redis/redis_module.cpp")
target_include_directories(ark_database_redis_plugin PRIVATE "${ROOT_DIR}/src/plugins")
target_link_libraries(ark_database_redis_plugin PRIVATE ark::redis arknet::arknet)
# Keep implementation symbols private so this exercises separate Asio executor
# type-erasure tables in the executable and dynamically loaded plugin.
set_target_properties(ark_database_redis_plugin PROPERTIES PREFIX ""
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
ark_database_test(ark_database_public_redis_plugin_tests public_redis_plugin.cpp)
target_link_libraries(ark_database_public_redis_plugin_tests PRIVATE ark::redis arknet::arknet)
target_compile_definitions(ark_database_public_redis_plugin_tests PRIVATE
    ARK_DATABASE_PLUGIN_DIR="$<TARGET_FILE_DIR:ark_database_redis_plugin>/")
add_dependencies(ark_database_public_redis_plugin_tests ark_database_redis_plugin)
# Real-service DSO acceptance: only public SDK headers and no implementation objects.
ark_database_test(ark_database_public_plugin_service_tests public_plugin_service.cpp)
set_property(TARGET ark_database_public_plugin_service_tests PROPERTY INCLUDE_DIRECTORIES "")
set_property(TARGET ark_database_public_plugin_service_tests PROPERTY LINK_LIBRARIES ark::redis Threads::Threads)
target_compile_definitions(ark_database_public_plugin_service_tests PRIVATE
    ARK_DATABASE_PLUGIN_DIR="$<TARGET_FILE_DIR:ark_database_redis_plugin>/")
add_dependencies(ark_database_public_plugin_service_tests ark_database_redis_plugin)
# Explicit real-service cancellation; compile in CI, execute only against owned services.
ark_database_test(ark_database_public_plugin_cancellation_tests public_plugin_cancellation.cpp)
set_property(TARGET ark_database_public_plugin_cancellation_tests PROPERTY INCLUDE_DIRECTORIES "")
set_property(TARGET ark_database_public_plugin_cancellation_tests PROPERTY LINK_LIBRARIES ark::redis Threads::Threads)
target_compile_definitions(ark_database_public_plugin_cancellation_tests PRIVATE
    ARK_DATABASE_PLUGIN_DIR="$<TARGET_FILE_DIR:ark_database_redis_plugin>/")
add_dependencies(ark_database_public_plugin_cancellation_tests ark_database_redis_plugin)
# Explicit owned-server fault acceptance; never execute this program in check-database.
ark_database_test(ark_database_public_plugin_server_termination_tests public_plugin_server_termination.cpp)
set_property(TARGET ark_database_public_plugin_server_termination_tests PROPERTY INCLUDE_DIRECTORIES "")
set_property(TARGET ark_database_public_plugin_server_termination_tests PROPERTY LINK_LIBRARIES ark::redis Threads::Threads)
target_compile_definitions(ark_database_public_plugin_server_termination_tests PRIVATE
    ARK_DATABASE_PLUGIN_DIR="$<TARGET_FILE_DIR:ark_database_redis_plugin>/")
add_dependencies(ark_database_public_plugin_server_termination_tests ark_database_redis_plugin)
# Explicit owned-service restart acceptance; public host with a separate plugin DSO.
ark_database_test(ark_database_public_plugin_restart_tests public_plugin_restart.cpp)
set_property(TARGET ark_database_public_plugin_restart_tests PROPERTY INCLUDE_DIRECTORIES "")
set_property(TARGET ark_database_public_plugin_restart_tests PROPERTY LINK_LIBRARIES ark::redis Threads::Threads)
target_compile_definitions(ark_database_public_plugin_restart_tests PRIVATE
    ARK_DATABASE_PLUGIN_DIR="$<TARGET_FILE_DIR:ark_database_redis_plugin>/")
add_dependencies(ark_database_public_plugin_restart_tests ark_database_redis_plugin)
# Compile and load each sample's real module and plugin assembly with a local
# provider that rejects configuration before coroutine or network work.
function(ark_database_sample_check database)
    set(sample "${database}_sample")
    set(library "ark_database_${database}_sample_check")
    set(runner "ark_database_${database}_sample_tests")
    add_library(${library} SHARED
        "${ROOT_DIR}/examples/${sample}/${sample}_module.cpp"
        "${ROOT_DIR}/examples/${sample}/${sample}_plugin.cpp")
    target_link_libraries(${library} PRIVATE ark::${database} arknet::arknet)
    set_target_properties(${library} PROPERTIES PREFIX ""
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    ark_database_test(${runner} sample_plugin.cpp)
    target_link_libraries(${runner} PRIVATE ark::${database} arknet::arknet)
    target_compile_definitions(${runner} PRIVATE
        ARK_DATABASE_SAMPLE_NAME="${sample}"
        ARK_DATABASE_SAMPLE_DIR="$<TARGET_FILE_DIR:${library}>/"
        ARK_DATABASE_SAMPLE_LIBRARY="${library}")
    if(database STREQUAL "mysql")
        target_compile_definitions(${runner} PRIVATE ARK_DATABASE_SAMPLE_MYSQL)
    endif()
    add_dependencies(${runner} ${library})
endfunction()
ark_database_sample_check(redis)
add_custom_target(check-database-samples
    COMMAND $<TARGET_FILE:ark_database_redis_sample_tests>
    DEPENDS ark_database_redis_sample_tests USES_TERMINAL)
add_custom_target(check-database
    COMMAND $<TARGET_FILE:ark_database_queue_tests>
    COMMAND $<TARGET_FILE:ark_database_redis_stop_tests>
    COMMAND $<TARGET_FILE:ark_database_public_redis_tests>
    COMMAND $<TARGET_FILE:ark_database_public_redis_plugin_tests>
    COMMAND $<TARGET_FILE:ark_database_task_tests>
    COMMAND $<TARGET_FILE:ark_database_operations_tests>
    COMMAND $<TARGET_FILE:ark_database_service_retirement_tests>
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:ark_database_task_contract_tests>
        -P "${CMAKE_CURRENT_SOURCE_DIR}/task_group_contract.cmake"
    DEPENDS ark_database_queue_tests ark_database_redis_stop_tests ark_database_public_redis_tests
        ark_database_public_redis_plugin_tests ark_database_task_tests ark_database_operations_tests
        ark_database_service_retirement_tests
        ark_database_task_contract_tests
    USES_TERMINAL)

# Compile real-service acceptance in ordinary CI without running any server work.
add_dependencies(check-database ark_database_redis_sample_check ark_database_public_service_tests
    ark_database_public_failure_service_tests ark_database_public_plugin_service_tests
    ark_database_public_plugin_cancellation_tests ark_database_public_plugin_server_termination_tests
    ark_database_public_plugin_restart_tests check-database-samples)

if(ARKNET_ENABLE_SSL)
    target_link_libraries(ark_database_public_failure_service_tests PRIVATE ark::mysql)
    target_link_libraries(ark_database_public_service_tests PRIVATE ark::mysql)
    target_sources(ark_database_public_failure_service_tests PRIVATE
        "${ROOT_DIR}/src/plugins/mysql/mysql_module.cpp")
    target_compile_definitions(ark_database_public_failure_service_tests PRIVATE
        ARK_DATABASE_UNTRUSTED_CA="${ARKNET_SOURCE_DIR}/tests/certs/wrong.pem")
    target_sources(ark_database_public_service_tests PRIVATE
        "${ROOT_DIR}/src/plugins/mysql/mysql_module.cpp")
    target_compile_definitions(ark_database_public_redis_tests PRIVATE
        ARK_DATABASE_REDIS_CERT_DIR="${ARKNET_SOURCE_DIR}/tests/certs")
    ark_database_test(ark_database_public_mysql_tests main.cpp public_mysql_module.cpp
        "${ROOT_DIR}/src/plugins/mysql/mysql_module.cpp")
    target_include_directories(ark_database_public_mysql_tests PRIVATE "${ARKNET_SOURCE_DIR}/tests")
    target_compile_definitions(ark_database_public_mysql_tests PRIVATE
        ARKNET_TEST_CERT_DIR="${ARKNET_SOURCE_DIR}/tests/certs")
    target_link_libraries(ark_database_public_mysql_tests PRIVATE ark::mysql arknet::arknet)
    add_library(ark_database_mysql_plugin SHARED
        "${ROOT_DIR}/src/plugins/mysql/mysql_plugin.cpp" "${ROOT_DIR}/src/plugins/mysql/mysql_module.cpp")
    target_include_directories(ark_database_mysql_plugin PRIVATE "${ROOT_DIR}/src/plugins")
    target_link_libraries(ark_database_mysql_plugin PRIVATE ark::mysql arknet::arknet)
    set_target_properties(ark_database_mysql_plugin PROPERTIES PREFIX ""
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    ark_database_test(ark_database_public_mysql_plugin_tests public_mysql_plugin.cpp)
    target_link_libraries(ark_database_public_mysql_plugin_tests PRIVATE ark::mysql arknet::arknet)
    target_compile_definitions(ark_database_public_mysql_plugin_tests PRIVATE
        ARK_DATABASE_PLUGIN_DIR="$<TARGET_FILE_DIR:ark_database_mysql_plugin>/")
    add_dependencies(ark_database_public_mysql_plugin_tests ark_database_mysql_plugin)
    add_dependencies(ark_database_public_plugin_service_tests ark_database_mysql_plugin)
    target_link_libraries(ark_database_public_plugin_service_tests PRIVATE ark::mysql)
    target_link_libraries(ark_database_public_plugin_cancellation_tests PRIVATE ark::mysql)
    add_dependencies(ark_database_public_plugin_cancellation_tests ark_database_mysql_plugin)
    target_link_libraries(ark_database_public_plugin_server_termination_tests PRIVATE ark::mysql)
    add_dependencies(ark_database_public_plugin_server_termination_tests ark_database_mysql_plugin)
    target_link_libraries(ark_database_public_plugin_restart_tests PRIVATE ark::mysql)
    add_dependencies(ark_database_public_plugin_restart_tests ark_database_mysql_plugin)
    ark_database_sample_check(mysql)
    add_custom_target(check-database-samples-mysql
        COMMAND $<TARGET_FILE:ark_database_mysql_sample_tests>
        DEPENDS ark_database_mysql_sample_tests USES_TERMINAL)
    add_dependencies(check-database-samples check-database-samples-mysql)
    add_custom_target(check-database-public-mysql
        COMMAND $<TARGET_FILE:ark_database_public_mysql_tests>
        COMMAND $<TARGET_FILE:ark_database_public_mysql_plugin_tests>
        DEPENDS ark_database_public_mysql_tests ark_database_public_mysql_plugin_tests ark_database_mysql_sample_check USES_TERMINAL)
    add_dependencies(check-database check-database-public-mysql)
    ark_database_test(ark_database_mysql_transaction_awaitable_tests main.cpp transactions_awaitable.cpp)
    target_link_libraries(ark_database_mysql_transaction_awaitable_tests PRIVATE arknet::arknet)
    add_custom_target(check-database-mysql-transaction-awaitable
        COMMAND $<TARGET_FILE:ark_database_mysql_transaction_awaitable_tests>
        DEPENDS ark_database_mysql_transaction_awaitable_tests USES_TERMINAL)
    add_dependencies(check-database check-database-mysql-transaction-awaitable)
    ark_database_test(ark_database_mysql_transaction_state_tests main.cpp mysql_transaction_state.cpp)
    target_link_libraries(ark_database_mysql_transaction_state_tests PRIVATE arknet::arknet)
    ark_database_test(ark_database_mysql_transaction_tests mysql_transactions.cpp)
    target_link_libraries(ark_database_mysql_transaction_tests PRIVATE arknet::arknet)
    add_custom_target(check-database-mysql-transaction-state
        COMMAND $<TARGET_FILE:ark_database_mysql_transaction_state_tests>
        DEPENDS ark_database_mysql_transaction_state_tests USES_TERMINAL)
    add_dependencies(check-database check-database-mysql-transaction-state)

    set(ark_database_redis_cert_dir "${ARKNET_SOURCE_DIR}/tests/certs")
    foreach(certificate ca.pem server.pem server-key.pem wrong.pem ipv6/ca.pem)
        if(NOT EXISTS "${ark_database_redis_cert_dir}/${certificate}")
            message(FATAL_ERROR "Redis TLS regression requires arknet public test fixtures: ${certificate}")
        endif()
    endforeach()
    ark_database_test(ark_database_redis_tls_tests main.cpp redis_service_tls.cpp)
    target_link_libraries(ark_database_redis_tls_tests PRIVATE arknet::arknet)
    target_compile_definitions(ark_database_redis_tls_tests PRIVATE
        ARK_DATABASE_REDIS_CERT_DIR="${ark_database_redis_cert_dir}")
    add_custom_target(check-database-redis-tls
        COMMAND $<TARGET_FILE:ark_database_redis_tls_tests>
        DEPENDS ark_database_redis_tls_tests USES_TERMINAL)
    add_dependencies(check-database check-database-redis-tls)
    if(NOT EXISTS "${ARKNET_SOURCE_DIR}/include/arknet/mysql/pool.hpp")
        message(FATAL_ERROR "MySQL development tests require a local arknet checkout with the native MySQL pool")
    endif()
    ark_database_test(ark_database_mysql_tests mysql_service.cpp)
    target_link_libraries(ark_database_mysql_tests PRIVATE arknet::arknet)
else()
    message(STATUS "Native MySQL development test disabled: enable ARKNET_ENABLE_SSL for authentication and TLS")
endif()
