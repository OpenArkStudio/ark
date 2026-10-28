set(loader_output_configurations ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
list(TRANSFORM loader_output_configurations TOUPPER)
list(REMOVE_DUPLICATES loader_output_configurations)

function(ark_native_loader_set_output target directory)
    foreach(kind RUNTIME LIBRARY ARCHIVE)
        set_property(TARGET ${target} PROPERTY ${kind}_OUTPUT_DIRECTORY "${directory}")
        # Root platform settings initialize these properties when targets are
        # created. They take precedence over the generic output directory.
        foreach(configuration IN LISTS loader_output_configurations)
            set_property(TARGET ${target} PROPERTY ${kind}_OUTPUT_DIRECTORY_${configuration}
                "${directory}/${configuration}")
        endforeach()
    endforeach()
endfunction()

add_library(ark_loader_sibling SHARED sibling.cpp)
set_property(TARGET ark_loader_sibling PROPERTY LINK_LIBRARIES "")
add_library(ark_loader_dependent SHARED dependent.cpp)
set_property(TARGET ark_loader_dependent PROPERTY LINK_LIBRARIES ark_loader_sibling)
set(loader_plugin_directory "${CMAKE_CURRENT_BINARY_DIR}/plugins")
set(loader_host_directory "${CMAKE_CURRENT_BINARY_DIR}/host")
foreach(target ark_loader_sibling ark_loader_dependent)
    set_target_properties(${target} PROPERTIES PREFIX "")
    ark_native_loader_set_output(${target} "${loader_plugin_directory}")
endforeach()
# The fixture's native dependency is resolved from its own directory on Unix.
# Production non-Windows loading and runtime library settings are unchanged.
if(APPLE)
    set_target_properties(ark_loader_dependent PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(ark_loader_dependent PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN")
endif()
add_executable(ark_native_loader_tests host.cpp)
set_property(TARGET ark_native_loader_tests PROPERTY LINK_LIBRARIES ark::core)
ark_native_loader_set_output(ark_native_loader_tests "${loader_host_directory}")

get_target_property(host_links ark_native_loader_tests LINK_LIBRARIES)
get_target_property(plugin_links ark_loader_dependent LINK_LIBRARIES)
get_target_property(host_directory ark_native_loader_tests RUNTIME_OUTPUT_DIRECTORY)
get_target_property(plugin_directory ark_loader_dependent RUNTIME_OUTPUT_DIRECTORY)
get_target_property(sibling_directory ark_loader_sibling RUNTIME_OUTPUT_DIRECTORY)
if(host_directory STREQUAL plugin_directory OR NOT sibling_directory STREQUAL plugin_directory OR
        NOT host_links STREQUAL "ark::core" OR NOT plugin_links STREQUAL "ark_loader_sibling")
    message(FATAL_ERROR "Native loader regression requires an unlinked host and sibling-only DLL dependency")
endif()
foreach(configuration IN LISTS loader_output_configurations)
    foreach(kind RUNTIME LIBRARY ARCHIVE)
        get_target_property(host_output ark_native_loader_tests ${kind}_OUTPUT_DIRECTORY_${configuration})
        get_target_property(plugin_output ark_loader_dependent ${kind}_OUTPUT_DIRECTORY_${configuration})
        get_target_property(sibling_output ark_loader_sibling ${kind}_OUTPUT_DIRECTORY_${configuration})
        if(NOT host_output STREQUAL "${loader_host_directory}/${configuration}" OR
                NOT plugin_output STREQUAL "${loader_plugin_directory}/${configuration}" OR
                NOT sibling_output STREQUAL plugin_output)
            message(FATAL_ERROR "Native loader ${configuration} ${kind} outputs must override inherited directories")
        endif()
    endforeach()
endforeach()
add_custom_target(check-native-loader
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_native_loader_tests>"
        "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_loader_dependent>"
        "-DDEPENDENCY=$<TARGET_FILE:ark_loader_sibling>"
        "-DWORKING_DIR=${CMAKE_CURRENT_BINARY_DIR}/working directory"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run.cmake"
    DEPENDS ark_native_loader_tests ark_loader_dependent ark_loader_sibling
    USES_TERMINAL VERBATIM)
