function(ark_validate_plugin_name plugin_name)
    if(NOT plugin_name MATCHES "^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
        message(FATAL_ERROR "Plugin library names must use lowercase snake_case: ${plugin_name}")
    endif()
endfunction()

function(BUILD_PLUGIN_MACRO project_name)
    ark_validate_plugin_name("${project_name}")
    if(UNIX AND NOT APPLE)
        set(CMAKE_INSTALL_RPATH ".")
        set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    endif()
    file(GLOB_RECURSE PLUGIN_SRC CONFIGURE_DEPENDS *.h *.hpp *.cpp *.cc *.c)
    get_filename_component(plugin_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    set(PLUGIN_HEADERS ${ark_${plugin_name}_public_headers})
    add_library(${project_name} SHARED ${PLUGIN_SRC} ${PLUGIN_HEADERS})
    set_target_properties(${project_name} PROPERTIES
        PREFIX ""
        FOLDER "plugins"
        ARCHIVE_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
    if(ARK_INSTALL)
        set_property(GLOBAL APPEND PROPERTY ARK_INSTALL_RUNTIME_TARGETS ${project_name})
        install(TARGETS ${project_name}
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
    add_dependencies(${project_name} AFProto)
    target_link_libraries(${project_name} ark::ark ${ARGN})
endfunction()

# Public plugin APIs can be consumed without building their shared libraries.
# All plugins keep their source headers beside their implementation.
# Explicit mappings preserve logical public paths independently of source layout.
# Kernel and the remaining legacy integrations retain every existing public
# header, including implementation and assembly headers. Earlier interface
# migrations keep their private assembly outside the public-header mapping.
set(ark_mysql_public_headers "${CMAKE_CURRENT_SOURCE_DIR}/mysql/mysql_module.hpp")
set(ark_redis_public_headers "${CMAKE_CURRENT_SOURCE_DIR}/redis/redis_module.hpp")
set(ark_utility_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/guid_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/timer_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/schedule_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/compress_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/scheduler.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/ctpl.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/utility/ctpl_stl.h"
)
set(ark_net_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/net/http_client_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/http_server.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_client_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_client_service_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_define.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_event.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_msg.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_server_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_server_service_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_service_manager_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/net_session.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/rudp_client.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/rudp_server.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_client.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_server.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/websocket_client.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/websocket_server.hpp"
)
set(ark_bus_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/bus/bus_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/bus/msg_module.hpp"
)
set(ark_http_public_headers "${CMAKE_CURRENT_SOURCE_DIR}/http/http_server_module.hpp")
set(ark_kernel_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity_json.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity_snapshot.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity_nested_snapshot.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity_stream.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/class_call_back_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/class_meta.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/class_meta_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/class_meta_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/class_meta_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/component.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/config_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/config_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/container.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/container_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/container_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/container_manager_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/container_meta.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/data.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/data_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/data_list.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/data_list_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/event_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/event_manager_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel_plugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/map_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/map_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/node.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/node_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/node_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/node_meta.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/row.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/static_entity.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/static_entity_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/static_entity_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/table.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/table_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/table_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/table_meta.hpp"
)
set(ark_consul_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/consul/consul_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/consul/consul_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/consul/consul_plugin.hpp"
)
set(ark_etcd_client_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/etcd_client/etcd_client.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/etcd_client/etcd_client_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/etcd_client/etcd_client_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/etcd_client/etcd_client_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/etcd_client/etcd_client_plugin.hpp"
)
set(ark_kafka_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/kafka/kafka.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kafka/kafka_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kafka/kafka_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kafka/kafka_plugin.hpp"
)
set(ark_osssdk_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/osssdk/oss_sdk_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/osssdk/oss_sdk_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/osssdk/oss_sdk_plugin.hpp"
)
set(ark_physic_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/physic_config_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/physic_config_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/physic_define.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/physic_module.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/physic_module_impl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/physic_plugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physic/wave_front.hpp"
)
set(ark_colocated_plugin_names mysql redis utility net bus http kernel consul etcd_client kafka osssdk physic)
set(ark_plugin_sdk_include_dir "${CMAKE_CURRENT_BINARY_DIR}/sdk-include")
foreach(plugin_name IN LISTS ark_colocated_plugin_names)
    set(staged_headers)
    set(staging_dir "${ark_plugin_sdk_include_dir}/ark/${plugin_name}")
    foreach(header IN LISTS ark_${plugin_name}_public_headers)
        get_filename_component(header_name "${header}" NAME)
        set(staged_header "${staging_dir}/${header_name}")
        configure_file("${header}" "${staged_header}" COPYONLY)
        if(ARK_INSTALL)
            install(FILES "${header}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ark/${plugin_name}")
        endif()
        list(APPEND staged_headers "${staged_header}")
    endforeach()
    # These managed directories are owned by the explicit mapping. This
    # also removes the last staged header when a plugin's list becomes empty.
    file(GLOB_RECURSE old_headers LIST_DIRECTORIES FALSE "${staging_dir}/*")
    foreach(header IN LISTS old_headers)
        if(NOT header IN_LIST staged_headers)
            file(REMOVE "${header}")
        endif()
    endforeach()
endforeach()
file(GLOB plugin_entries CONFIGURE_DEPENDS LIST_DIRECTORIES TRUE "${CMAKE_CURRENT_SOURCE_DIR}/*")
set(ark_plugin_names)
foreach(plugin_dir IN LISTS plugin_entries)
    if(IS_DIRECTORY "${plugin_dir}" AND EXISTS "${plugin_dir}/CMakeLists.txt")
        get_filename_component(plugin_name "${plugin_dir}" NAME)
        list(APPEND ark_plugin_names "${plugin_name}")
        add_library(ark_${plugin_name}_api INTERFACE)
        add_library(ark::${plugin_name} ALIAS ark_${plugin_name}_api)
        if(NOT plugin_name IN_LIST ark_colocated_plugin_names)
            message(FATAL_ERROR "Plugin ${plugin_name} needs an explicit public-header mapping")
        endif()
        target_include_directories(ark_${plugin_name}_api INTERFACE
            "$<BUILD_INTERFACE:${ark_plugin_sdk_include_dir}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
        target_link_libraries(ark_${plugin_name}_api INTERFACE ark::core)
        target_link_libraries(ark_sdk INTERFACE ark::${plugin_name})
    endif()
endforeach()

# Propagate API header dependencies, separately from runtime library selection.
foreach(plugin_name IN LISTS ark_plugin_names)
    set(api_headers ${ark_${plugin_name}_public_headers})
    set(api_dependencies)
    foreach(header IN LISTS api_headers)
        file(STRINGS "${header}" includes REGEX "#[ \t]*include[ \t]*[<\"]ark/")
        if(ARK_BUILD_PROTO AND includes MATCHES "ark/proto\\.hpp")
            list(APPEND api_dependencies ark::proto)
        endif()
        foreach(include IN LISTS includes)
            if(include MATCHES "ark/([a-z0-9_]+)/" AND CMAKE_MATCH_1 IN_LIST ark_plugin_names AND NOT CMAKE_MATCH_1 STREQUAL plugin_name)
                list(APPEND api_dependencies "ark::${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()
    if(api_dependencies)
        list(REMOVE_DUPLICATES api_dependencies)
        target_link_libraries(ark_${plugin_name}_api INTERFACE ${api_dependencies})
    endif()
    string(TOUPPER "${plugin_name}" plugin_option)
    set(plugin_default OFF)
    if(plugin_name MATCHES "^(kernel|net|bus|utility|etcd_client)$")
        set(plugin_default ON)
    endif()
    option(ARK_BUILD_PLUGIN_${plugin_option} "Build the ${plugin_name} plugin" ${plugin_default})
endforeach()

if((ARK_BUILD_APPLICATIONS AND ARK_BUILD_PLUGIN_KERNEL) OR ARK_BUILD_CONFIG_TOOL OR
        (ARK_BUILD_CORE_TESTS AND ARK_BUILD_PROTO))
    include("${CMAKE_CURRENT_SOURCE_DIR}/kernel/entity_json.cmake")
endif()

if(ARK_BUILD_APPLICATIONS)
    foreach(plugin_name IN LISTS ark_plugin_names)
        string(TOUPPER "${plugin_name}" plugin_option)
        if(ARK_BUILD_PLUGIN_${plugin_option})
            add_subdirectory("${plugin_name}")
        endif()
    endforeach()
endif()

# The root package exports the same public targets as source-tree consumers.
set(ark_plugin_names "${ark_plugin_names}" PARENT_SCOPE)
