find_package(nlohmann_json CONFIG REQUIRED)
BUILD_PLUGIN_MACRO("guest_room_plugin")
set_property(TARGET guest_room_plugin APPEND PROPERTY LINK_LIBRARIES ark::proto ark::kernel ark::net)
if(UNIX AND NOT APPLE AND TARGET protobuf::libprotobuf)
    get_target_property(ark_room_proto_type protobuf::libprotobuf TYPE)
    set(ark_room_private_proto OFF)
    if(ark_room_proto_type STREQUAL "STATIC_LIBRARY")
        set(ark_room_private_proto ON)
    elseif(ark_room_proto_type STREQUAL "UNKNOWN_LIBRARY")
        # FindProtobuf's module fallback may not record the imported library type.
        string(TOUPPER "${CMAKE_BUILD_TYPE}" ark_room_configuration)
        get_target_property(ark_room_proto_configurations protobuf::libprotobuf IMPORTED_CONFIGURATIONS)
        set(ark_room_proto_locations IMPORTED_LOCATION_${ark_room_configuration} IMPORTED_LOCATION)
        foreach(configuration IN LISTS ark_room_proto_configurations)
            string(TOUPPER "${configuration}" configuration)
            list(APPEND ark_room_proto_locations IMPORTED_LOCATION_${configuration})
        endforeach()
        foreach(property IN LISTS ark_room_proto_locations)
            get_target_property(ark_room_proto_location protobuf::libprotobuf ${property})
            if(ark_room_proto_location)
                if(ark_room_proto_location MATCHES "\\.a$")
                    set(ark_room_private_proto ON)
                endif()
                break()
            endif()
        endforeach()
    endif()
    if(ark_room_private_proto)
        # ELF interposition must not share generated descriptor initialization
        # flags with a host that owns another private static generated_pool.
        # Isolate archive symbols and inline codec/reflection entry points, while
        # retaining public non-inline module/plugin entry points and RTTI.
        # A shared Protobuf runtime already owns one common pool; leave it alone.
        target_link_options(guest_room_plugin PRIVATE "LINKER:--exclude-libs,ALL")
        set_property(TARGET guest_room_plugin PROPERTY VISIBILITY_INLINES_HIDDEN YES)
    endif()
endif()

add_executable(ark_guest_room_codec_tests ../../tests/core/main.cpp ../../tests/guest_room/codec.cpp)
set_property(TARGET ark_guest_room_codec_tests PROPERTY LINK_LIBRARIES ark::net ark::kernel ark::proto nlohmann_json::nlohmann_json)
add_custom_target(check-guest-room-codec
    COMMAND $<TARGET_FILE:ark_guest_room_codec_tests> "--test-case=*"
    DEPENDS ark_guest_room_codec_tests USES_TERMINAL VERBATIM)
target_compile_definitions(ark_guest_room_codec_tests PRIVATE
    ARK_GUEST_ROOM_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/wire-vectors.json")

if(UNIX)
    foreach(plugin kernel utility bus net)
        if(NOT TARGET ark_${plugin}_plugin)
            string(TOUPPER "${plugin}" plugin_option)
            message(FATAL_ERROR
                "check-guest-room-server requires ark_${plugin}_plugin; enable ARK_BUILD_RUNTIME_PLUGINS and ARK_BUILD_PLUGIN_${plugin_option}")
        endif()
    endforeach()
    add_executable(ark_guest_room_server_tests ../../tests/guest_room/server.cpp)
    set_property(TARGET ark_guest_room_server_tests PROPERTY LINK_LIBRARIES ark::net ark::kernel ark::proto nlohmann_json::nlohmann_json)
    # The controlled host observes this example's concrete type. Launcher child
    # processes still load their own plugin independently through the manifest.
    target_link_libraries(ark_guest_room_server_tests PRIVATE guest_room_plugin)
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
