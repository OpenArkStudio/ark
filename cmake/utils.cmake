# Get ARK version from VERSION and put it in ARK_VERSION
function(ark_extract_version)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" file_contents)
    string(REGEX MATCH "ARK_VER_MAJOR ([0-9]+)" _ "${file_contents}")
    if (NOT CMAKE_MATCH_COUNT EQUAL 1)
        message(FATAL_ERROR "Could not extract major version number from ARK/VERSION")
    endif ()
    set(ver_major ${CMAKE_MATCH_1})

    string(REGEX MATCH "ARK_VER_MINOR ([0-9]+)" _ "${file_contents}")
    if (NOT CMAKE_MATCH_COUNT EQUAL 1)
        message(FATAL_ERROR "Could not extract minor version number from ARK/VERSION")
    endif ()

    set(ver_minor ${CMAKE_MATCH_1})
    string(REGEX MATCH "ARK_VER_PATCH ([0-9]+)" _ "${file_contents}")
    if (NOT CMAKE_MATCH_COUNT EQUAL 1)
        message(FATAL_ERROR "Could not extract patch version number from ARK/VERSION")
    endif ()
    set(ver_patch ${CMAKE_MATCH_1})

    set(ARK_VERSION "${ver_major}.${ver_minor}.${ver_patch}" PARENT_SCOPE)
endfunction()

function(MY_PROTOBUF_GENERATE_CPP PATH SRCS HDRS)
    cmake_parse_arguments(ark_proto "" "EXPORT_DECL" "" ${ARGN})
    if (ark_proto_KEYWORDS_MISSING_VALUES OR NOT ark_proto_UNPARSED_ARGUMENTS)
        message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
        return()
    endif ()
    # Keep the legacy command arguments unchanged when no export is requested.
    set(ark_proto_cpp_args --cpp_out ${PATH})
    if (ark_proto_EXPORT_DECL)
        if (NOT ark_proto_EXPORT_DECL MATCHES "^[A-Za-z_][A-Za-z_0-9]*$")
            message(SEND_ERROR "Error: EXPORT_DECL must be a C++ identifier")
            return()
        endif ()
        set(ark_proto_cpp_args --cpp_out "dllexport_decl=${ark_proto_EXPORT_DECL}:${PATH}")
    endif ()

    if (PROTOBUF_GENERATE_CPP_APPEND_PATH)
        # Create an include path for each file specified
        foreach (FIL ${ark_proto_UNPARSED_ARGUMENTS})
            get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
            get_filename_component(ABS_PATH ${ABS_FIL} PATH)
            list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
            if (${_contains_already} EQUAL -1)
                list(APPEND _protobuf_include_path -I ${ABS_PATH})
            endif ()
        endforeach ()
    else ()
        set(_protobuf_include_path -I ${CMAKE_CURRENT_SOURCE_DIR})
    endif ()

    if (DEFINED PROTOBUF_IMPORT_DIRS)
        foreach (DIR ${PROTOBUF_IMPORT_DIRS})
            get_filename_component(ABS_PATH ${DIR} ABSOLUTE)
            list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
            if (${_contains_already} EQUAL -1)
                list(APPEND _protobuf_include_path -I ${ABS_PATH})
            endif ()
        endforeach ()
    endif ()

    set(${SRCS})
    set(${HDRS})
    foreach (FIL ${ark_proto_UNPARSED_ARGUMENTS})
        get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
        get_filename_component(FIL_WE ${FIL} NAME_WE)

        list(APPEND ${SRCS} "${PATH}/${FIL_WE}.pb.cc")
        list(APPEND ${HDRS} "${PATH}/${FIL_WE}.pb.h")

        execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${PATH})

        add_custom_command(
                OUTPUT "${PATH}/${FIL_WE}.pb.cc"
                "${PATH}/${FIL_WE}.pb.h"
                COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
                ARGS ${ark_proto_cpp_args} ${_protobuf_include_path} ${ABS_FIL}
                DEPENDS ${ABS_FIL}
                COMMENT "Running C++ protocol buffer compiler on ${FIL}"
                VERBATIM)
    endforeach ()

    set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
    set(${SRCS} ${${SRCS}} PARENT_SCOPE)
    set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()
