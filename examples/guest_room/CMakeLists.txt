find_package(nlohmann_json CONFIG REQUIRED)
BUILD_PLUGIN_MACRO("guest_room_plugin")
set_property(TARGET guest_room_plugin APPEND PROPERTY LINK_LIBRARIES ark::proto ark::kernel ark::net)

add_executable(ark_guest_room_codec_tests ../../tests/core/main.cpp ../../tests/guest_room/codec.cpp)
set_property(TARGET ark_guest_room_codec_tests PROPERTY LINK_LIBRARIES ark::net ark::kernel ark::proto nlohmann_json::nlohmann_json)
add_custom_target(check-guest-room-codec
    COMMAND $<TARGET_FILE:ark_guest_room_codec_tests> "--test-case=*"
    DEPENDS ark_guest_room_codec_tests USES_TERMINAL VERBATIM)
target_compile_definitions(ark_guest_room_codec_tests PRIVATE
    ARK_GUEST_ROOM_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/wire-vectors.json")

if(UNIX)
    add_executable(ark_guest_room_server_tests ../../tests/guest_room/server.cpp)
    set_property(TARGET ark_guest_room_server_tests PROPERTY LINK_LIBRARIES ark::net ark::kernel ark::proto nlohmann_json::nlohmann_json)
    add_custom_target(check-guest-room-server
        COMMAND ${CMAKE_COMMAND} "-DBIN_DIR=$<TARGET_FILE_DIR:guest_room_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-runtime" -DHOST=127.0.0.1 -DPORT=19191
            -P "${CMAKE_CURRENT_SOURCE_DIR}/stage.cmake"
        COMMAND $<TARGET_FILE:ark_guest_room_server_tests> "$<TARGET_FILE:app>"
            "${CMAKE_CURRENT_BINARY_DIR}/native-runtime" 19191
        DEPENDS app guest_room_plugin ark_kernel_plugin ark_utility_plugin ark_bus_plugin ark_net_plugin
            ark_guest_room_server_tests
        USES_TERMINAL VERBATIM)
    add_custom_target(check-guest-room DEPENDS check-guest-room-codec check-guest-room-server)
else()
    add_custom_target(check-guest-room DEPENDS check-guest-room-codec)
endif()
