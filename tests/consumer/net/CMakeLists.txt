cmake_minimum_required(VERSION 3.21)
project(ark_net_sdk_consumer LANGUAGES CXX)
get_filename_component(ARK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(ARK_BUILD_APPLICATIONS OFF CACHE BOOL "Build applications" FORCE)
set(ARK_BUILD_PROTO ON CACHE BOOL "Build protocol messages" FORCE)
set(ARK_BUILD_NETWORK_TESTS OFF CACHE BOOL "Build network tests" FORCE)
set(ARK_BUILD_CORE_TESTS OFF CACHE BOOL "Build core tests" FORCE)
set(ARK_BUILD_TESTS OFF CACHE BOOL "Build legacy tests" FORCE)
set(ARK_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
add_subdirectory("${ARK_SOURCE_DIR}" ark)

# Compile each retained public header in isolation through only the Net API target.
set(net_headers
    http_client_module.hpp
    http_server.hpp
    net.hpp
    net_client_service.hpp
    net_client_service_impl.hpp
    net_define.hpp
    net_event.hpp
    net_msg.hpp
    net_server_service.hpp
    net_server_service_impl.hpp
    net_service_manager_module.hpp
    net_session.hpp
    rudp_client.hpp
    rudp_server.hpp
    tcp_client.hpp
    tcp_server.hpp
    websocket_client.hpp
    websocket_server.hpp
)
set(header_checks)
foreach(header IN LISTS net_headers)
    string(MAKE_C_IDENTIFIER "${header}" identifier)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${identifier}.cpp")
    file(WRITE "${source}" "#include <ark/net/${header}>\n")
    list(APPEND header_checks "${source}")
endforeach()
add_executable(ark_net_sdk_consumer main.cpp ${header_checks})
target_link_libraries(ark_net_sdk_consumer PRIVATE ark::net)
add_custom_target(check-sdk
    COMMAND $<TARGET_FILE:ark_net_sdk_consumer>
    DEPENDS ark_net_sdk_consumer USES_TERMINAL)
