add_executable(ark_network_tests
    main.cpp
    framing.cpp
    logging.cpp
    endpoint_lifetime.cpp
    tcp_websocket.cpp
    rudp.cpp
    http.cpp
    ../../src/plugins/net/tcp_client.cpp
    ../../src/plugins/net/tcp_server.cpp
    ../../src/plugins/net/websocket_client.cpp
    ../../src/plugins/net/websocket_server.cpp
    ../../src/plugins/net/rudp_client.cpp
    ../../src/plugins/net/rudp_server.cpp
    ../../src/plugins/net/http_client_module_impl.cpp
    ../../src/plugins/net/http_server.cpp
    ../../src/plugins/http/http_server_module_impl.cpp
    ../../src/plugins/http/http_plugin.cpp)
target_link_libraries(ark_network_tests PRIVATE ark::config tomlplusplus::tomlplusplus)
target_include_directories(ark_network_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
add_custom_target(check-network
    COMMAND $<TARGET_FILE:ark_network_tests>
    DEPENDS ark_network_tests
    USES_TERMINAL)

# Keep allocation-failure injection isolated from transport and framing tests.
add_executable(ark_net_message_tests main.cpp message_ownership.cpp framing.cpp)
set_property(TARGET ark_net_message_tests PROPERTY LINK_LIBRARIES ark::net)
add_executable(ark_net_message_failure_tests main.cpp message_failure.cpp)
set_property(TARGET ark_net_message_failure_tests PROPERTY LINK_LIBRARIES ark::net)
add_custom_target(check-net-message
    COMMAND $<TARGET_FILE:ark_net_message_tests>
    COMMAND $<TARGET_FILE:ark_net_message_failure_tests>
    DEPENDS ark_net_message_tests ark_net_message_failure_tests
    USES_TERMINAL VERBATIM)
add_dependencies(check-network check-net-message)

# Observe private HTTP reservations in a separate production translation unit;
# global allocation replacement must not affect the other network regressions.
add_executable(ark_http_startup_failure_tests main.cpp http_startup_failure.cpp
    ../../src/plugins/http/http_plugin.cpp)
target_link_libraries(ark_http_startup_failure_tests PRIVATE ark::config tomlplusplus::tomlplusplus)
target_include_directories(ark_http_startup_failure_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
add_custom_target(check-http-startup-failure
    COMMAND $<TARGET_FILE:ark_http_startup_failure_tests>
    DEPENDS ark_http_startup_failure_tests USES_TERMINAL VERBATIM)
add_dependencies(check-network check-http-startup-failure)
