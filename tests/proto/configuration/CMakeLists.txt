cmake_minimum_required(VERSION 3.21)
project(ark_proto_configuration_contract LANGUAGES CXX)
include("${ARK_SOURCE_DIR}/cmake/utils.cmake")

# Exercise the production CMake branches, not a Windows compiler/linker. Only
# protoc generation is built here; Windows DSO acceptance remains a hosted check.
set(WIN32 "${TEST_WIN32}")
set(PROTOBUF_GENERATE_CPP_APPEND_PATH ON)
set(PROTOBUF_PROTOC_EXECUTABLE "${PROTOC}")
add_library(proto_runtime_fixture INTERFACE)
add_library(protobuf::libprotobuf ALIAS proto_runtime_fixture)
set(PROTOBUF_LIBRARIES protobuf::libprotobuf)
set(BIN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(ARK_INSTALL OFF)
add_subdirectory("${ARK_SOURCE_DIR}/src/proto" proto)

get_target_property(actual_type AFProto TYPE)
get_target_property(private_definitions AFProto COMPILE_DEFINITIONS)
get_target_property(public_definitions AFProto INTERFACE_COMPILE_DEFINITIONS)
get_target_property(public_libraries AFProto INTERFACE_LINK_LIBRARIES)
get_target_property(runtime_directory AFProto RUNTIME_OUTPUT_DIRECTORY)
get_target_property(prefix AFProto PREFIX)
get_target_property(aliased_target ark::proto ALIASED_TARGET)
if(NOT runtime_directory STREQUAL BIN_OUTPUT_DIR OR NOT prefix STREQUAL "" OR NOT aliased_target STREQUAL "AFProto")
    message(FATAL_ERROR "Protocol runtime directory, library stem or public alias changed")
endif()
if(NOT "ARK_HAS_PROTOBUF=1" IN_LIST public_definitions OR NOT "protobuf::libprotobuf" IN_LIST public_libraries)
    message(FATAL_ERROR "Protocol consumers lost the public Protobuf usage requirements")
endif()
if(TEST_WIN32)
    if(NOT actual_type STREQUAL "SHARED_LIBRARY" OR
            NOT "ARK_PROTO_EXPORT=__declspec(dllexport)" IN_LIST private_definitions OR
            "ARK_PROTO_EXPORT=__declspec(dllimport)" IN_LIST private_definitions OR
            NOT "ARK_PROTO_EXPORT=__declspec(dllimport)" IN_LIST public_definitions OR
            "ARK_PROTO_EXPORT=__declspec(dllexport)" IN_LIST public_definitions)
        message(FATAL_ERROR "Windows protocol target must export its own messages and import them in consumers")
    endif()
else()
    if(NOT actual_type STREQUAL "STATIC_LIBRARY" OR
            "${private_definitions};${public_definitions}" MATCHES "ARK_PROTO_EXPORT")
        message(FATAL_ERROR "Non-Windows protocol target must retain its static, unannotated contract")
    endif()
endif()

add_library(proto_consumer_fixture INTERFACE)
target_link_libraries(proto_consumer_fixture INTERFACE ark::proto)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer-definitions.txt"
    CONTENT "$<TARGET_PROPERTY:proto_consumer_fixture,INTERFACE_COMPILE_DEFINITIONS>")
get_target_property(generated_sources AFProto SOURCES)
add_custom_target(generate-protocol-contract DEPENDS ${generated_sources})
