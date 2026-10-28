set(project_name AFProto)

set(ark_proto_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB PROTO_DEFINITION CONFIGURE_DEPENDS proto/*.proto)
if(WIN32)
    # A host and its loaded plugins must share generated message descriptors.
    MY_PROTOBUF_GENERATE_CPP("${ark_proto_generated}/ark/proto" PROTO_SRC PROTO_HEADER
        EXPORT_DECL ARK_PROTO_EXPORT ${PROTO_DEFINITION})
    add_library(${project_name} SHARED ${PROTO_HEADER} ${PROTO_SRC})
    target_compile_definitions(${project_name}
        PRIVATE "ARK_PROTO_EXPORT=__declspec(dllexport)"
        INTERFACE "ARK_PROTO_EXPORT=__declspec(dllimport)")
else()
    MY_PROTOBUF_GENERATE_CPP("${ark_proto_generated}/ark/proto" PROTO_SRC PROTO_HEADER ${PROTO_DEFINITION})
    add_library(${project_name} STATIC ${PROTO_HEADER} ${PROTO_SRC})
endif()
add_library(ark::proto ALIAS ${project_name})
target_include_directories(${project_name} PUBLIC
    "$<BUILD_INTERFACE:${ark_proto_generated}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if(ARK_INSTALL)
    install(FILES ${PROTO_HEADER} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ark/proto")
endif()
target_compile_definitions(${project_name} PUBLIC ARK_HAS_PROTOBUF=1)
target_link_libraries(${project_name} PUBLIC ${PROTOBUF_LIBRARIES})
set_target_properties(${project_name} PROPERTIES
    PREFIX ""
    FOLDER "proto"
    ARCHIVE_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
