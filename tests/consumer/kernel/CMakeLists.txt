cmake_minimum_required(VERSION 3.21)
project(ark_kernel_header_consumer LANGUAGES CXX)
get_filename_component(ARK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(ARK_BUILD_APPLICATIONS OFF CACHE BOOL "Build applications" FORCE)
set(ARK_BUILD_PROTO ON CACHE BOOL "Build protocol messages")
set(ARK_BUILD_CONFIG OFF CACHE BOOL "Build configuration reader" FORCE)
set(ARK_BUILD_NETWORK_TESTS OFF CACHE BOOL "Build network tests" FORCE)
set(ARK_BUILD_CORE_TESTS OFF CACHE BOOL "Build core tests" FORCE)
set(ARK_BUILD_TESTS OFF CACHE BOOL "Build legacy tests" FORCE)
set(ARK_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
add_subdirectory("${ARK_SOURCE_DIR}" ark)

# Compile every retained public Kernel header through its interface target.
set(kernel_headers
    class_call_back_manager.hpp
    class_meta.hpp
    class_meta_manager.hpp
    class_meta_module.hpp
    class_meta_module_impl.hpp
    component.hpp
    config_module.hpp
    config_module_impl.hpp
    container.hpp
    container_impl.hpp
    container_manager.hpp
    container_manager_impl.hpp
    container_meta.hpp
    data.hpp
    data_impl.hpp
    data_list.hpp
    data_list_impl.hpp
    entity.hpp
    entity_impl.hpp
    entity_json.hpp
    entity_snapshot.hpp
    entity_nested_snapshot.hpp
    entity_stream.hpp
    event_manager.hpp
    event_manager_impl.hpp
    kernel_module.hpp
    kernel_module_impl.hpp
    kernel_plugin.hpp
    map_module.hpp
    map_module_impl.hpp
    node.hpp
    node_impl.hpp
    node_manager.hpp
    node_meta.hpp
    row.hpp
    static_entity.hpp
    static_entity_impl.hpp
    static_entity_manager.hpp
    table.hpp
    table_impl.hpp
    table_manager.hpp
    table_meta.hpp
)
if(NOT ARK_BUILD_PROTO)
    # These existing headers require ark/proto.hpp; value/template headers do not.
    list(REMOVE_ITEM kernel_headers kernel_module.hpp kernel_module_impl.hpp map_module_impl.hpp)
endif()
set(header_checks)
foreach(header IN LISTS kernel_headers)
    string(MAKE_C_IDENTIFIER "${header}" identifier)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${identifier}.cpp")
    set(prerequisites "")
    if(header STREQUAL "container_meta.hpp")
        # This unchanged legacy header expects these declarations from its caller.
        set(prerequisites "#include <ark/define.hpp>\n#include <ark/enum.hpp>\n")
    endif()
    file(WRITE "${source}" "${prerequisites}#include <ark/kernel/${header}>\n")
    list(APPEND header_checks "${source}")
endforeach()
add_executable(ark_kernel_header_consumer main.cpp ${header_checks})
target_link_libraries(ark_kernel_header_consumer PRIVATE ark::kernel)
add_custom_target(check-kernel-headers
    COMMAND $<TARGET_FILE:ark_kernel_header_consumer>
    DEPENDS ark_kernel_header_consumer USES_TERMINAL)
