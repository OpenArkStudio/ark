add_library(ark_core_header_checks OBJECT headers/queue.cpp headers/guid128.cpp headers/rw_lock.cpp
    headers/array_map.cpp headers/bit_value.cpp headers/consistent_hash.cpp headers/list.cpp
    headers/lock_free_queue.cpp headers/lru_map.cpp headers/map.cpp headers/thread_safe_queue.cpp
    headers/object_vector.cpp headers/error_code.cpp headers/logger.cpp headers/murmur_hash.cpp)
target_link_libraries(ark_core_header_checks PRIVATE ark::core)

add_subdirectory(hash_header)

add_executable(ark_core_tests main.cpp strings.cpp containers.cpp pod_arrays.cpp templates.cpp callbacks.cpp
    header_regressions.cpp cxx20_language.cpp concurrency.cpp plugin_manager_startup.cpp module_dependencies.cpp data_strings.cpp data_length_bounds.cpp node_data_storage.cpp
    object_vectors.cpp plugin_construction.cpp module_factory.cpp random_generator.cpp xxtea.cpp murmur_hash.cpp)
target_link_libraries(ark_core_tests PRIVATE ark::core ark::kernel ark::utility)
add_dependencies(ark_core_tests ark_core_header_checks ark_murmur_hash_standalone_check)
add_library(ark_construction_header_checks OBJECT headers/module_base.cpp headers/plugin_base.cpp)
set_property(TARGET ark_construction_header_checks PROPERTY LINK_LIBRARIES ark::core)
add_dependencies(ark_core_tests ark_construction_header_checks)
if(ARK_BUILD_PROTO)
    add_executable(ark_net_external_root_tests main.cpp net_external_root.cpp)
    set_property(TARGET ark_net_external_root_tests PROPERTY LINK_LIBRARIES ark::net)
    add_custom_target(check-net-external-root
        COMMAND $<TARGET_FILE:ark_net_external_root_tests>
        DEPENDS ark_net_external_root_tests USES_TERMINAL VERBATIM)
    target_sources(ark_core_tests PRIVATE protobuf_messages.cpp)
    target_link_libraries(ark_core_tests PRIVATE ark::bus ark::proto)
    file(GLOB_RECURSE ark_proto_contract_files CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/proto/proto/*.proto"
        "${PROJECT_SOURCE_DIR}/resources/photon/proto/*.proto")
    list(LENGTH ark_proto_contract_files ark_proto_source_count)
    set(ark_proto_descriptor_set "${CMAKE_CURRENT_BINARY_DIR}/project-protocol-descriptors.pb")
    add_custom_command(OUTPUT "${ark_proto_descriptor_set}"
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
            "--proto_path=${PROJECT_SOURCE_DIR}/src/proto/proto"
            "--proto_path=${PROJECT_SOURCE_DIR}/resources/photon/proto"
            "--descriptor_set_out=${ark_proto_descriptor_set}"
            --include_imports ${ark_proto_contract_files}
        DEPENDS ${ark_proto_contract_files}
        COMMENT "Generate project Protobuf descriptors for naming and compatibility checks"
        VERBATIM)
    add_custom_target(ark_proto_contract_descriptors DEPENDS "${ark_proto_descriptor_set}")
    add_executable(ark_proto_naming_tests main.cpp protobuf_messages.cpp ../proto/naming.cpp)
    set_property(TARGET ark_proto_naming_tests PROPERTY LINK_LIBRARIES ark::bus ark::proto)
    target_compile_definitions(ark_proto_naming_tests PRIVATE
        ARK_PROTO_DESCRIPTOR_SET="${ark_proto_descriptor_set}"
        ARK_PROTO_SOURCE_COUNT=${ark_proto_source_count})
    add_dependencies(ark_proto_naming_tests ark_proto_contract_descriptors)
    add_custom_target(check-proto-naming
        COMMAND $<TARGET_FILE:ark_proto_naming_tests>
        DEPENDS ark_proto_naming_tests USES_TERMINAL VERBATIM)
    add_custom_target(check-proto-configuration
        COMMAND ${CMAKE_COMMAND} "-DARK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPROTOC=${PROTOBUF_PROTOC_EXECUTABLE}"
            "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/proto configuration"
            -P "${PROJECT_SOURCE_DIR}/tests/proto/check_configuration.cmake"
        USES_TERMINAL VERBATIM)
endif()
add_custom_target(check-core
    COMMAND $<TARGET_FILE:ark_core_tests>
    DEPENDS ark_core_tests
    USES_TERMINAL)
if(TARGET check-net-external-root)
    add_dependencies(check-core check-net-external-root)
endif()

# Keep singleton shutdown terminal within its own process, including the logger specialization.
add_executable(ark_singleton_tests ../test_singleton.cpp)
set_property(TARGET ark_singleton_tests PROPERTY LINK_LIBRARIES ark::core)
add_custom_target(check-singleton
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_singleton_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/singleton.cmake"
    DEPENDS ark_singleton_tests USES_TERMINAL VERBATIM)
add_dependencies(check-core check-singleton)
if(TARGET check-proto-configuration)
    add_dependencies(check-core check-proto-configuration check-proto-naming)
endif()

set(ark_node_copy_test_sources main.cpp node_copy_ownership.cpp
    ../../src/plugins/kernel/node_manager.cpp
    ../../src/plugins/kernel/class_meta.cpp
    ../../src/plugins/kernel/class_call_back_manager.cpp
    ../../src/plugins/kernel/table_meta.cpp)
add_executable(ark_node_copy_tests ${ark_node_copy_test_sources})
set_property(TARGET ark_node_copy_tests PROPERTY LINK_LIBRARIES ark::kernel)
add_executable(ark_node_copy_hook_tests ${ark_node_copy_test_sources})
set_property(TARGET ark_node_copy_hook_tests PROPERTY LINK_LIBRARIES ark::kernel)
target_compile_definitions(ark_node_copy_hook_tests PRIVATE NDEBUG ARK_NODE_COPY_TEST_HOOKS)
# Identical hook expansion in every TU avoids mixing different inline definitions.
if(MSVC)
    target_compile_options(ark_node_copy_hook_tests PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/node_copy_hooks.hpp")
else()
    target_compile_options(ark_node_copy_hook_tests PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/node_copy_hooks.hpp")
endif()
add_custom_target(check-node-copy
    COMMAND $<TARGET_FILE:ark_node_copy_tests>
    COMMAND $<TARGET_FILE:ark_node_copy_hook_tests>
    DEPENDS ark_node_copy_tests ark_node_copy_hook_tests USES_TERMINAL VERBATIM)
add_dependencies(check-core check-node-copy)

# Default nothrow new[] failure injection stays isolated from ordinary Core tests.
add_executable(ark_data_allocation_tests main.cpp data_allocation_failure.cpp)
set_property(TARGET ark_data_allocation_tests PROPERTY LINK_LIBRARIES ark::kernel)
add_executable(ark_data_allocation_hook_tests main.cpp data_allocation_hooks.cpp)
set_property(TARGET ark_data_allocation_hook_tests PROPERTY LINK_LIBRARIES ark::kernel)
add_executable(ark_data_guid32_tests main.cpp data_allocation_failure.cpp data_strings.cpp data_length_bounds.cpp node_data_storage.cpp)
set_property(TARGET ark_data_guid32_tests PROPERTY LINK_LIBRARIES ark::kernel)
target_compile_definitions(ark_data_guid32_tests PRIVATE ARK_USE_GUID_32)
add_custom_target(check-data-allocation
    COMMAND $<TARGET_FILE:ark_data_allocation_tests>
    COMMAND $<TARGET_FILE:ark_data_allocation_hook_tests>
    COMMAND $<TARGET_FILE:ark_data_guid32_tests>
    DEPENDS ark_data_allocation_tests ark_data_allocation_hook_tests ark_data_guid32_tests USES_TERMINAL VERBATIM)
add_dependencies(check-core check-data-allocation)

add_executable(ark_entity_stream_tests main.cpp entity_stream.cpp entity_nested_stream.cpp)
set_property(TARGET ark_entity_stream_tests PROPERTY LINK_LIBRARIES ark::kernel)
add_custom_target(check-entity-stream
    COMMAND $<TARGET_FILE:ark_entity_stream_tests>
    DEPENDS ark_entity_stream_tests USES_TERMINAL)
add_dependencies(check-core check-entity-stream)
add_executable(ark_scalar_stream_allocation_tests main.cpp entity_scalar_stream_allocation.cpp)
set_property(TARGET ark_scalar_stream_allocation_tests PROPERTY LINK_LIBRARIES ark::kernel)
add_custom_target(check-scalar-stream-allocation
    COMMAND $<TARGET_FILE:ark_scalar_stream_allocation_tests>
    DEPENDS ark_scalar_stream_allocation_tests USES_TERMINAL)
add_dependencies(check-entity-stream check-scalar-stream-allocation)
add_executable(ark_nested_stream_allocation_tests main.cpp entity_nested_stream_allocation.cpp)
set_property(TARGET ark_nested_stream_allocation_tests PROPERTY LINK_LIBRARIES ark::kernel)
add_custom_target(check-nested-stream-allocation
    COMMAND $<TARGET_FILE:ark_nested_stream_allocation_tests>
    DEPENDS ark_nested_stream_allocation_tests USES_TERMINAL)
add_dependencies(check-entity-stream check-nested-stream-allocation)

add_executable(ark_logging_tests main.cpp logging_paths.cpp logging_source.cpp)
target_link_libraries(ark_logging_tests PRIVATE ark::core)
add_dependencies(ark_logging_tests ark_core_header_checks)
add_custom_target(check-logging
    COMMAND $<TARGET_FILE:ark_logging_tests>
    DEPENDS ark_logging_tests
    USES_TERMINAL)
add_dependencies(check-core check-logging)

# Separate binaries keep custom allocation hooks and new[] replacement isolated.
add_executable(ark_buffer_tests main.cpp buffers.cpp)
set_property(TARGET ark_buffer_tests PROPERTY LINK_LIBRARIES ark::core)
add_executable(ark_buffer_failure_tests main.cpp buffer_failure.cpp)
set_property(TARGET ark_buffer_failure_tests PROPERTY LINK_LIBRARIES ark::core)
add_executable(ark_buffer_hook_tests main.cpp buffer_hooks.cpp)
set_property(TARGET ark_buffer_hook_tests PROPERTY LINK_LIBRARIES ark::core)
add_custom_target(check-buffer
    COMMAND $<TARGET_FILE:ark_buffer_tests>
    COMMAND $<TARGET_FILE:ark_buffer_failure_tests>
    COMMAND $<TARGET_FILE:ark_buffer_hook_tests>
    DEPENDS ark_buffer_tests ark_buffer_failure_tests ark_buffer_hook_tests
    USES_TERMINAL VERBATIM)
add_dependencies(check-core check-buffer)

# Isolate timer allocation hooks and Release-mode assertion fallback checks.
add_executable(ark_timer_tests main.cpp timers.cpp)
set_property(TARGET ark_timer_tests PROPERTY LINK_LIBRARIES ark::core)
add_executable(ark_timer_hook_tests main.cpp timer_hooks.cpp)
set_property(TARGET ark_timer_hook_tests PROPERTY LINK_LIBRARIES ark::core)
target_compile_definitions(ark_timer_hook_tests PRIVATE NDEBUG)
add_custom_target(check-timer
    COMMAND $<TARGET_FILE:ark_timer_tests>
    COMMAND $<TARGET_FILE:ark_timer_hook_tests>
    DEPENDS ark_timer_tests ark_timer_hook_tests USES_TERMINAL VERBATIM)
add_executable(ark_timer_module_init_tests main.cpp timer_module_init.cpp
    "${PROJECT_SOURCE_DIR}/src/plugins/utility/timer_module_impl.cpp")
set_property(TARGET ark_timer_module_init_tests PROPERTY LINK_LIBRARIES ark::core ark::utility)
target_include_directories(ark_timer_module_init_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
# Apply NDEBUG to the real production cpp as well as the isolated test harness.
target_compile_definitions(ark_timer_module_init_tests PRIVATE NDEBUG)
add_custom_target(check-timer-module-init
    COMMAND $<TARGET_FILE:ark_timer_module_init_tests>
    DEPENDS ark_timer_module_init_tests USES_TERMINAL VERBATIM)
add_dependencies(check-timer check-timer-module-init)
add_dependencies(check-core check-timer)

# Isolate global new[] failure injection from the ordinary Core executables.
add_executable(ark_pod_array_failure_tests main.cpp pod_array_failure.cpp)
set_property(TARGET ark_pod_array_failure_tests PROPERTY LINK_LIBRARIES ark::core)
add_custom_target(check-pod-array-failure
    COMMAND $<TARGET_FILE:ark_pod_array_failure_tests>
    DEPENDS ark_pod_array_failure_tests USES_TERMINAL VERBATIM)
add_dependencies(check-core check-pod-array-failure)

if(ARK_BUILD_PROTO)
    # Every TU uses the same allocation/delete hooks; ordinary Kernel binaries remain independent.
    add_executable(ark_entity_custom_ownership_tests main.cpp entity_custom_ownership.cpp
        ../../src/plugins/kernel/class_call_back_manager.cpp
        ../../src/plugins/kernel/class_meta.cpp
        ../../src/plugins/kernel/container_impl.cpp
        ../../src/plugins/kernel/container_manager_impl.cpp
        ../../src/plugins/kernel/entity_impl.cpp
        ../../src/plugins/kernel/node_manager.cpp
        ../../src/plugins/kernel/row.cpp
        ../../src/plugins/kernel/table_impl.cpp
        ../../src/plugins/kernel/table_meta.cpp)
    target_link_libraries(ark_entity_custom_ownership_tests PRIVATE ark::kernel)
    target_compile_definitions(ark_entity_custom_ownership_tests PRIVATE NDEBUG)
    set(entity_custom_hooks "${CMAKE_CURRENT_SOURCE_DIR}/entity_custom_hooks.hpp")
    target_compile_options(ark_entity_custom_ownership_tests PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/FI${entity_custom_hooks}>"
        "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-include${entity_custom_hooks}>")
    add_custom_target(check-entity-custom-ownership
        COMMAND $<TARGET_FILE:ark_entity_custom_ownership_tests>
        DEPENDS ark_entity_custom_ownership_tests USES_TERMINAL)
    add_dependencies(check-core check-entity-custom-ownership)
endif()

find_package(Threads REQUIRED)
add_library(ark_task_header_checks OBJECT headers/task.cpp)
# This SDK consumer uses the public core target without aggregate plugin APIs.
set_property(TARGET ark_task_header_checks PROPERTY LINK_LIBRARIES ark::core)

add_executable(ark_task_tests main.cpp tasks.cpp)
set_property(TARGET ark_task_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
target_include_directories(ark_task_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
add_dependencies(ark_task_tests ark_task_header_checks)
add_library(ark_task_scheduler_header_checks OBJECT headers/task_scheduler.cpp headers/scheduler_task.cpp)
set_property(TARGET ark_task_scheduler_header_checks PROPERTY LINK_LIBRARIES ark::core ark::utility)
add_dependencies(ark_task_tests ark_task_scheduler_header_checks)

add_executable(ark_scheduler_drain_tests main.cpp scheduler_drain.cpp
    ../../src/plugins/utility/schedule_module_impl.cpp)
set_property(TARGET ark_scheduler_drain_tests PROPERTY LINK_LIBRARIES ark::core ark::utility Threads::Threads)
target_include_directories(ark_scheduler_drain_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins/utility")
add_custom_target(check-scheduler-drain
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_scheduler_drain_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scheduler_drain.cmake"
    DEPENDS ark_scheduler_drain_tests USES_TERMINAL VERBATIM)
add_dependencies(check-core check-scheduler-drain)

add_executable(ark_task_contract_tests ../database/task_group_contract.cpp)
set_property(TARGET ark_task_contract_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
add_custom_target(check-task-sdk
    COMMAND $<TARGET_FILE:ark_task_tests>
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_task_contract_tests>"
        -P "${PROJECT_SOURCE_DIR}/tests/database/task_group_contract.cmake"
    DEPENDS ark_task_tests ark_task_contract_tests
    USES_TERMINAL)
add_dependencies(check-core check-task-sdk)

add_executable(ark_business_runtime_tests main.cpp business_runtime.cpp)
set_property(TARGET ark_business_runtime_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
target_include_directories(ark_business_runtime_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
add_library(ark_business_runtime_header_check OBJECT headers/business_runtime.cpp)
set_property(TARGET ark_business_runtime_header_check PROPERTY LINK_LIBRARIES ark::core)
add_dependencies(ark_business_runtime_tests ark_business_runtime_header_check)
add_executable(ark_business_runtime_contract_tests business_runtime_contract.cpp)
set_property(TARGET ark_business_runtime_contract_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
add_custom_target(check-business-runtime
    COMMAND $<TARGET_FILE:ark_business_runtime_tests>
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_business_runtime_contract_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/business_runtime_contract.cmake"
    DEPENDS ark_business_runtime_tests ark_business_runtime_contract_tests USES_TERMINAL)
add_dependencies(check-core check-business-runtime)

add_executable(ark_app_shutdown_tests ../app/shutdown_request.cpp)
target_compile_features(ark_app_shutdown_tests PRIVATE cxx_std_20)
target_include_directories(ark_app_shutdown_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/application")
# This app-private state test does not consume the directory's inherited ark SDK.
set_property(TARGET ark_app_shutdown_tests PROPERTY LINK_LIBRARIES Threads::Threads)
add_custom_target(check-app-shutdown
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_app_shutdown_tests>"
        -P "${PROJECT_SOURCE_DIR}/tests/app/shutdown_request.cmake"
    DEPENDS ark_app_shutdown_tests
    USES_TERMINAL)
add_dependencies(check-core check-app-shutdown)

add_executable(ark_xml_tests main.cpp xml.cpp)
target_link_libraries(ark_xml_tests PRIVATE ark::core)
target_compile_definitions(ark_xml_tests PRIVATE ARK_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_custom_target(check-xml
    COMMAND $<TARGET_FILE:ark_xml_tests>
    DEPENDS ark_xml_tests
    USES_TERMINAL)
add_dependencies(check-core check-xml)

if(ARK_BUILD_CONFIG)
    add_library(ark_config_shared_probe SHARED ../consumer/config/shared.cpp)
    set_property(TARGET ark_config_shared_probe PROPERTY LINK_LIBRARIES ark::config tomlplusplus::tomlplusplus)
    target_include_directories(ark_config_shared_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
    target_compile_definitions(ark_config_shared_probe PRIVATE ARK_CONFIG_PROBE_BUILD)

    add_executable(ark_application_manifest_tests main.cpp application_manifest.cpp ../consumer/config/shared_library.cpp)
    # These configuration-only consumers deliberately exclude the inherited network SDK.
    set_property(TARGET ark_application_manifest_tests PROPERTY LINK_LIBRARIES ark::config ark_config_shared_probe)
    target_compile_definitions(ark_application_manifest_tests PRIVATE ARK_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_custom_target(check-application-manifest
        COMMAND $<TARGET_FILE:ark_application_manifest_tests>
        DEPENDS ark_application_manifest_tests
        USES_TERMINAL)
    add_dependencies(check-core check-application-manifest)

    add_executable(ark_plugin_configuration_tests main.cpp plugin_configuration.cpp)
    set_property(TARGET ark_plugin_configuration_tests PROPERTY LINK_LIBRARIES ark::config tomlplusplus::tomlplusplus)
    target_include_directories(ark_plugin_configuration_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
    target_compile_features(ark_plugin_configuration_tests PRIVATE cxx_std_20)
    add_custom_target(check-plugin-configuration
        COMMAND $<TARGET_FILE:ark_plugin_configuration_tests>
        DEPENDS ark_plugin_configuration_tests USES_TERMINAL)
    add_dependencies(check-core check-plugin-configuration)

    add_executable(ark_bus_configuration_tests main.cpp bus_configuration.cpp
        ../../src/plugins/bus/bus_module_impl.cpp)
    set_property(TARGET ark_bus_configuration_tests PROPERTY LINK_LIBRARIES ark::bus ark::config tomlplusplus::tomlplusplus)
    target_include_directories(ark_bus_configuration_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
    target_compile_definitions(ark_bus_configuration_tests PRIVATE ARK_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_custom_target(check-bus-configuration
        COMMAND $<TARGET_FILE:ark_bus_configuration_tests>
        DEPENDS ark_bus_configuration_tests USES_TERMINAL)
    add_dependencies(check-core check-bus-configuration)

    if(TARGET app AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
        add_custom_target(check-app-bus-configuration
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_bus_plugin>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/bus configuration app"
                -P "${PROJECT_SOURCE_DIR}/tests/app/bus_configuration.cmake"
            DEPENDS app ark_bus_plugin ark_net_plugin ark_utility_plugin USES_TERMINAL)
        add_dependencies(check-core check-app-bus-configuration)
    endif()

    if(TARGET app)
        add_library(ark_business_runtime_probe SHARED ../app/business_runtime_plugin.cpp)
        set_property(TARGET ark_business_runtime_probe PROPERTY LINK_LIBRARIES ark::core)
        target_include_directories(ark_business_runtime_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
        set_target_properties(ark_business_runtime_probe PROPERTIES PREFIX "")
        add_custom_target(check-app-business-runtime
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_business_runtime_probe>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/business runtime app"
                -P "${PROJECT_SOURCE_DIR}/tests/app/business_runtime.cmake"
            DEPENDS app ark_business_runtime_probe USES_TERMINAL)
        add_dependencies(check-core check-app-business-runtime)
        add_custom_target(check-app-shutdown-deadlines
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_business_runtime_probe>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/shutdown deadlines app"
                -P "${PROJECT_SOURCE_DIR}/tests/app/shutdown_deadlines.cmake"
            DEPENDS app ark_business_runtime_probe USES_TERMINAL)
        add_dependencies(check-core check-app-shutdown-deadlines)

        add_library(ark_app_startup_failure_plugin SHARED ../app/startup_failure_plugin.cpp)
        set_property(TARGET ark_app_startup_failure_plugin PROPERTY LINK_LIBRARIES ark::core)
        set_target_properties(ark_app_startup_failure_plugin PROPERTIES PREFIX "")
        add_library(ark_app_missing_entry_plugin SHARED ../app/missing_entry_plugin.cpp)
        set_property(TARGET ark_app_missing_entry_plugin PROPERTY LINK_LIBRARIES ark::core)
        set_target_properties(ark_app_missing_entry_plugin PROPERTIES PREFIX "")
        add_custom_target(check-app-startup-failures
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_app_startup_failure_plugin>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/startup failure runtime"
                -P "${PROJECT_SOURCE_DIR}/tests/app/startup_failures.cmake"
            DEPENDS app ark_app_startup_failure_plugin ark_app_missing_entry_plugin
            USES_TERMINAL)
        add_dependencies(check-core check-app-startup-failures)

        if(TARGET timers_plugin)
            add_custom_target(check-app-module-dependencies
                COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                    "-DPLUGIN_DIR=$<TARGET_FILE_DIR:timers_plugin>"
                    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                    "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/module dependency app"
                    -P "${PROJECT_SOURCE_DIR}/tests/app/module_dependencies.cmake"
                DEPENDS app timers_plugin USES_TERMINAL VERBATIM)
            add_dependencies(check-core check-app-module-dependencies)
        endif()

        add_custom_target(check-app-manifest-failures
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/manifest failure runtime"
                -P "${PROJECT_SOURCE_DIR}/tests/app/manifest_failures.cmake"
            DEPENDS app
            USES_TERMINAL)
        add_dependencies(check-core check-app-manifest-failures)

        if(TARGET ark_utility_plugin AND TARGET timers_plugin)
            add_custom_target(check-app-presets
                COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                    "-DUTILITY_PLUGIN=$<TARGET_FILE:ark_utility_plugin>"
                    "-DSAMPLE_PLUGIN=$<TARGET_FILE:timers_plugin>"
                    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                    "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/preset runtime 'quoted' #"
                    -P "${PROJECT_SOURCE_DIR}/tests/app/application_presets.cmake"
                DEPENDS app ark_utility_plugin timers_plugin USES_TERMINAL VERBATIM)
            if(UNIX)
                add_executable(ark_app_preset_stop_tests ../app/application_preset_stop.cpp)
                set_property(TARGET ark_app_preset_stop_tests PROPERTY LINK_LIBRARIES Threads::Threads)
                target_compile_features(ark_app_preset_stop_tests PRIVATE cxx_std_20)
                add_custom_target(check-app-preset-stop
                    COMMAND $<TARGET_FILE:ark_app_preset_stop_tests>
                        "${PROJECT_SOURCE_DIR}/tests/app/application_preset_stop.sh"
                    DEPENDS ark_app_preset_stop_tests USES_TERMINAL VERBATIM)
                add_dependencies(check-app-presets check-app-preset-stop)
            endif()
            add_dependencies(check-core check-app-presets)
        endif()
    endif()
endif()

if(ARK_BUILD_PROTO)
    add_executable(ark_kernel_initialization_tests main.cpp module_initialization.cpp kernel_resource_ownership.cpp table_row_allocation.cpp entity_config_copy.cpp entity_json_loading.cpp kernel_delayed_sync.cpp entity_nested_snapshot.cpp entity_snapshot.cpp entity_snapshot_interface.cpp
        ../../src/plugins/kernel/class_call_back_manager.cpp
        ../../src/plugins/kernel/class_meta.cpp
        ../../src/plugins/kernel/class_meta_module_impl.cpp
        ../../src/plugins/kernel/config_module_impl.cpp
        ../../src/plugins/kernel/container_impl.cpp
        ../../src/plugins/kernel/container_manager_impl.cpp
        ../../src/plugins/kernel/entity_impl.cpp
        ../../src/plugins/kernel/entity_snapshot.cpp
        ../../src/plugins/kernel/kernel_module_impl.cpp
        ../../src/plugins/kernel/map_module_impl.cpp
        ../../src/plugins/kernel/node_manager.cpp
        ../../src/plugins/kernel/row.cpp
        ../../src/plugins/kernel/static_entity_impl.cpp
        ../../src/plugins/kernel/table_impl.cpp
        ../../src/plugins/kernel/table_meta.cpp)
    target_link_libraries(ark_kernel_initialization_tests PRIVATE ark::kernel ark::utility ark::proto ark::kernel_json)
    if(TARGET ark_game_plugin AND TARGET ark_kernel_plugin AND TARGET ark_bus_plugin AND TARGET ark_utility_plugin)
        target_sources(ark_kernel_initialization_tests PRIVATE ../server/game_initialization.cpp
            ../../src/server/game/game_plugin.cpp ../../src/server/game/game_net_module_impl.cpp
            ../../src/plugins/bus/bus_module_impl.cpp ../../src/plugins/utility/guid_module_impl.cpp)
        target_include_directories(ark_kernel_initialization_tests PRIVATE
            "${PROJECT_SOURCE_DIR}/src/server/game" "${PROJECT_SOURCE_DIR}/src/plugins")
        target_link_libraries(ark_kernel_initialization_tests PRIVATE ark::bus ark::net ark::config tomlplusplus::tomlplusplus)
        add_custom_target(check-game-initialization
            COMMAND $<TARGET_FILE:ark_kernel_initialization_tests> "--test-case=Game*"
            DEPENDS ark_kernel_initialization_tests USES_TERMINAL VERBATIM)
        add_dependencies(check-server-plugins check-game-initialization)
    endif()
    # CI configures Debug, whose duplicate-ID guard aborts instead of returning.
    # Compile the exercised implementation and its dependencies with NDEBUG in an
    # independent executable; ark::kernel supplies interfaces, not runtime code.
    add_executable(ark_kernel_resource_ownership_tests main.cpp kernel_resource_ownership.cpp
        ../../src/plugins/kernel/class_call_back_manager.cpp
        ../../src/plugins/kernel/class_meta.cpp
        ../../src/plugins/kernel/class_meta_module_impl.cpp
        ../../src/plugins/kernel/config_module_impl.cpp
        ../../src/plugins/kernel/node_manager.cpp
        ../../src/plugins/kernel/static_entity_impl.cpp
        ../../src/plugins/kernel/table_meta.cpp)
    target_compile_definitions(ark_kernel_resource_ownership_tests PRIVATE NDEBUG)
    target_link_libraries(ark_kernel_resource_ownership_tests PRIVATE ark::kernel ark::kernel_json)
    # Isolate nothrow allocation replacement and compile the real implementation
    # with NDEBUG even when CI configures the ordinary Kernel regressions in Debug.
    add_executable(ark_table_row_allocation_tests main.cpp table_row_allocation.cpp
        ../../src/plugins/kernel/class_meta.cpp
        ../../src/plugins/kernel/node_manager.cpp
        ../../src/plugins/kernel/row.cpp
        ../../src/plugins/kernel/table_impl.cpp
        ../../src/plugins/kernel/table_meta.cpp)
    target_compile_definitions(ark_table_row_allocation_tests PRIVATE NDEBUG ARK_TABLE_ROW_FAILURE_TEST)
    target_link_libraries(ark_table_row_allocation_tests PRIVATE ark::kernel)
    add_custom_target(check-table-row-allocation
        COMMAND $<TARGET_FILE:ark_table_row_allocation_tests>
        DEPENDS ark_table_row_allocation_tests USES_TERMINAL)
    add_custom_target(check-kernel-initialization
        COMMAND $<TARGET_FILE:ark_kernel_initialization_tests>
        COMMAND $<TARGET_FILE:ark_kernel_resource_ownership_tests>
        DEPENDS ark_kernel_initialization_tests ark_kernel_resource_ownership_tests
        USES_TERMINAL)
    add_dependencies(check-kernel-initialization check-table-row-allocation)
    add_dependencies(check-core check-kernel-initialization)
endif()

add_subdirectory(../osssdk "${CMAKE_CURRENT_BINARY_DIR}/osssdk")

add_executable(ark_native_unload_tests main.cpp dynamic_library.cpp)
set_property(TARGET ark_native_unload_tests PROPERTY LINK_LIBRARIES ark::core)
foreach(probe ark_plugin_allocation_first ark_plugin_allocation_second)
    add_library(${probe} SHARED plugin_allocation_entry.cpp)
    set_property(TARGET ${probe} PROPERTY LINK_LIBRARIES ark::core)
    set_target_properties(${probe} PROPERTIES PREFIX "")
endforeach()
target_compile_definitions(ark_native_unload_tests PRIVATE
    ARK_UNLOAD_PROBE_DIR="$<TARGET_FILE_DIR:ark_oss_sdk_entry_probe>/"
    ARK_ALLOCATION_PROBE_DIR="$<TARGET_FILE_DIR:ark_plugin_allocation_first>/")
add_custom_target(check-native-unload
    COMMAND $<TARGET_FILE:ark_native_unload_tests>
    DEPENDS ark_native_unload_tests ark_oss_sdk_entry_probe ark_plugin_allocation_first ark_plugin_allocation_second
    USES_TERMINAL VERBATIM)
add_dependencies(check-core check-native-unload)
add_subdirectory(native_loader)
add_dependencies(check-native-unload check-native-loader)

if(ARK_BUILD_PROTO AND ARK_BUILD_CONFIG)
    add_subdirectory(../consul "${CMAKE_CURRENT_BINARY_DIR}/consul")
endif()

# Keep JSON optional: construction checks follow the selected runtime plugin.
if(TARGET ark_etcd_client_plugin)
    add_subdirectory(../etcd_client "${CMAKE_CURRENT_BINARY_DIR}/etcd_client")
endif()

# Keep Kafka driver discovery optional: checks follow its selected runtime plugin.
if(TARGET ark_kafka_plugin)
    add_subdirectory(../kafka "${CMAKE_CURRENT_BINARY_DIR}/kafka")
endif()

if(ARK_BUILD_PROTO AND TARGET ark_kernel_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_kernel_plugin_tests ../app/kernel_plugin.cpp)
    set_property(TARGET ark_kernel_plugin_tests PROPERTY LINK_LIBRARIES ark::kernel)
    add_custom_target(check-kernel-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_kernel_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_kernel_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/kernel plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/kernel_plugin.cmake"
        DEPENDS ark_kernel_plugin_tests ark_kernel_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-kernel-plugin)
endif()

if(ARK_BUILD_CONFIG AND TARGET timers_plugin AND TARGET module_lifecycle_plugin AND TARGET entity_operations_plugin AND
        TARGET ark_utility_plugin AND TARGET ark_kernel_plugin)
    add_executable(ark_typed_examples_tests ../app/typed_examples.cpp)
    set_property(TARGET ark_typed_examples_tests PROPERTY LINK_LIBRARIES ark::core ark::config ark::utility ark::kernel)
    add_custom_target(check-typed-examples
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_typed_examples_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:timers_plugin>" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/typed examples runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/typed_examples.cmake"
        DEPENDS ark_typed_examples_tests timers_plugin module_lifecycle_plugin entity_operations_plugin ark_utility_plugin ark_kernel_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-typed-examples)
endif()

if(ARK_BUILD_PROTO AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_net_plugin_tests ../app/net_plugin.cpp)
    set_property(TARGET ark_net_plugin_tests PROPERTY LINK_LIBRARIES ark::net)
    add_custom_target(check-net-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_net_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_net_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/net plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/net_plugin.cmake"
        DEPENDS ark_net_plugin_tests ark_net_plugin ark_bus_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-net-plugin)
endif()

if(ARK_BUILD_PROTO AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_http_client_drain_tests ../app/http_client_drain.cpp)
    set_property(TARGET ark_http_client_drain_tests PROPERTY LINK_LIBRARIES ark::net)
    add_custom_target(check-net-http-drain
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_http_client_drain_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_net_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/http client drain runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/http_client_drain.cmake"
        DEPENDS ark_http_client_drain_tests ark_net_plugin ark_bus_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-net-http-drain)
endif()

if(ARK_BUILD_PROTO AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_bus_plugin_tests ../app/bus_plugin.cpp)
    set_property(TARGET ark_bus_plugin_tests PROPERTY LINK_LIBRARIES ark::bus)
    add_custom_target(check-bus-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_bus_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_bus_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/bus plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/bus_plugin.cmake"
        DEPENDS ark_bus_plugin_tests ark_bus_plugin ark_net_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-bus-plugin)
    if(UNIX)
        add_executable(ark_static_services_tests ../app/static_services.cpp)
        set_property(TARGET ark_static_services_tests PROPERTY LINK_LIBRARIES ark::bus)
        add_custom_target(check-static-services
            COMMAND $<TARGET_FILE:ark_static_services_tests> "$<TARGET_FILE_DIR:ark_bus_plugin>"
                "${CMAKE_CURRENT_BINARY_DIR}/static services runtime"
            DEPENDS ark_static_services_tests ark_bus_plugin ark_net_plugin ark_utility_plugin
            USES_TERMINAL VERBATIM)
        add_dependencies(check-core check-static-services)
    endif()
endif()

if(TARGET ark_http_plugin)
    add_executable(ark_http_plugin_tests ../app/http_plugin.cpp)
    set_property(TARGET ark_http_plugin_tests PROPERTY LINK_LIBRARIES ark::http)
    add_custom_target(check-http-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_http_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_http_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/http plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/http_plugin.cmake"
        DEPENDS ark_http_plugin_tests ark_http_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-http-plugin)
endif()

if(TARGET check-server-plugins)
    add_dependencies(check-core check-server-plugins)
endif()

# Legacy HTTP demonstrations remain excluded from normal applications/examples.
# Core validation uses the real sources/DSOs when their public prerequisites exist.
set(ark_http_examples_default OFF)
if(NOT ASIO_STANDALONE AND TARGET ark::http AND TARGET ark::net AND TARGET ark::etcd_client)
    set(ark_http_examples_default ON)
endif()
option(ARK_BUILD_HTTP_EXAMPLE_TESTS "Build HTTP example construction regressions" ${ark_http_examples_default})
if(ARK_BUILD_HTTP_EXAMPLE_TESTS)
    if(ASIO_STANDALONE OR NOT TARGET ark::http OR NOT TARGET ark::net OR NOT TARGET ark::etcd_client)
        message(FATAL_ERROR "HTTP example tests require Boost.Asio/Beast and ark::http, ark::net, ark::etcd_client")
    endif()
    add_subdirectory("${PROJECT_SOURCE_DIR}/examples/http_client" "${CMAKE_CURRENT_BINARY_DIR}/http_client_example")
    add_subdirectory("${PROJECT_SOURCE_DIR}/examples/http_server" "${CMAKE_CURRENT_BINARY_DIR}/http_server_example")
    add_executable(ark_http_examples_tests ../app/http_examples.cpp
        ../../examples/http_client/http_client_sample_module.cpp
        ../../examples/http_server/http_server_module.cpp)
    set_property(TARGET ark_http_examples_tests PROPERTY LINK_LIBRARIES ark::http ark::net ark::etcd_client)
    add_custom_target(check-http-examples
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_http_examples_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:http_server_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/http examples runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/http_examples.cmake"
        DEPENDS ark_http_examples_tests http_client_sample_plugin http_server_plugin USES_TERMINAL)
    add_dependencies(check-core check-http-examples)
endif()

if(TARGET check-guest-room)
    add_dependencies(check-core check-guest-room)
endif()

if(TARGET check-entity-json)
    add_dependencies(check-core check-entity-json)
endif()

# Startup-failure cleanup is independent of ordinary stop/uninstall. Keep global
# allocation replacement isolated, with hard process limits for emergency exit.
add_executable(ark_startup_cleanup_tests main.cpp startup_cleanup.cpp plugin_manager_startup.cpp module_dependencies.cpp)
set_property(TARGET ark_startup_cleanup_tests PROPERTY LINK_LIBRARIES ark::core)
add_executable(ark_startup_cleanup_allocation_tests ../app/startup_cleanup_allocation.cpp)
set_property(TARGET ark_startup_cleanup_allocation_tests PROPERTY LINK_LIBRARIES ark::core)
add_custom_target(check-startup-cleanup
    COMMAND $<TARGET_FILE:ark_startup_cleanup_tests>
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_startup_cleanup_allocation_tests>"
        "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/startup-cleanup-allocation"
        -P "${PROJECT_SOURCE_DIR}/tests/app/startup_cleanup_allocation.cmake"
    DEPENDS ark_startup_cleanup_tests ark_startup_cleanup_allocation_tests USES_TERMINAL VERBATIM)
if(TARGET app)
    add_library(ark_app_startup_cleanup_plugin SHARED ../app/startup_cleanup_plugin.cpp)
    set_property(TARGET ark_app_startup_cleanup_plugin PROPERTY LINK_LIBRARIES ark::core)
    set_target_properties(ark_app_startup_cleanup_plugin PROPERTIES PREFIX "")
    add_custom_target(check-app-startup-cleanup
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_app_startup_cleanup_plugin>"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/startup cleanup runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/startup_cleanup.cmake"
        DEPENDS app ark_app_startup_cleanup_plugin USES_TERMINAL VERBATIM)
    add_dependencies(check-startup-cleanup check-app-startup-cleanup)
endif()
add_dependencies(check-core check-startup-cleanup)
add_library(ark_startup_cleanup_header_check OBJECT headers/startup_cleanup.cpp)
set_property(TARGET ark_startup_cleanup_header_check PROPERTY LINK_LIBRARIES ark::core)
add_dependencies(ark_startup_cleanup_tests ark_startup_cleanup_header_check)
