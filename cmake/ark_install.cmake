include(CMakePackageConfigHelpers)

# Keep the installed names identical to the source-tree aliases.
set(ark_package_targets ark_core ark_sdk)
install(TARGETS ark_config_headers EXPORT arkConfigHeaderTargets)
install(EXPORT arkConfigHeaderTargets NAMESPACE ark:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ark")
set_target_properties(ark_config_headers PROPERTIES EXPORT_NAME config_headers)
set_target_properties(ark_core PROPERTIES EXPORT_NAME core)
set_target_properties(ark_sdk PROPERTIES EXPORT_NAME ark)
set(ark_package_components config_headers core ark)
foreach(plugin_name IN LISTS ark_plugin_names)
    set_target_properties(ark_${plugin_name}_api PROPERTIES EXPORT_NAME "${plugin_name}")
    list(APPEND ark_package_targets ark_${plugin_name}_api)
    list(APPEND ark_package_components "${plugin_name}")
endforeach()
set(ARK_PACKAGE_HAS_CONFIG OFF)
set(ARK_PACKAGE_NEEDS_TOML OFF)
if(TARGET ark_config)
    set(ARK_PACKAGE_HAS_CONFIG ON)
    get_target_property(ARK_PACKAGE_NEEDS_TOML ark_config ARK_PACKAGE_NEEDS_TOML)
    if(NOT ARK_PACKAGE_NEEDS_TOML)
        set(ARK_PACKAGE_NEEDS_TOML OFF)
    endif()
    set_target_properties(ark_config PROPERTIES EXPORT_NAME config)
    install(TARGETS ark_config EXPORT arkConfigReaderTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(EXPORT arkConfigReaderTargets NAMESPACE ark:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ark")
    list(APPEND ark_package_components config)
endif()
set(ARK_PACKAGE_HAS_PROTO OFF)
set(ARK_PACKAGE_HAS_KERNEL_JSON OFF)
if(TARGET ark_kernel_json)
    set(ARK_PACKAGE_HAS_KERNEL_JSON ON)
    list(APPEND ark_package_components kernel_json)
endif()
if(TARGET AFProto)
    set(ARK_PACKAGE_HAS_PROTO ON)
    set_target_properties(AFProto PROPERTIES EXPORT_NAME proto)
    list(APPEND ark_package_targets AFProto)
    list(APPEND ark_package_components proto)
endif()
install(TARGETS ${ark_package_targets} EXPORT arkTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
if(TARGET app)
    install(TARGETS app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
set(ark_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/ark")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/arkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/arkConfig.cmake"
    INSTALL_DESTINATION "${ark_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/arkConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(EXPORT arkTargets NAMESPACE ark:: DESTINATION "${ark_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/arkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/arkConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindRapidXML.cmake"
    DESTINATION "${ark_package_dir}")
install(FILES LICENSE README.md README_CN.md DESTINATION "${CMAKE_INSTALL_DATADIR}/ark")
# These are samples, not a deployment populated with business data or credentials.
install(DIRECTORY config/ DESTINATION "${CMAKE_INSTALL_DATADIR}/ark/config"
    FILES_MATCHING PATTERN "*.toml" PATTERN "*.conf" PATTERN "*.xml")

add_custom_target(check-install
    COMMAND "${CMAKE_COMMAND}"
        "-DARK_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DARK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DARK_TEST_CONFIG=$<CONFIG>"
        "-DARK_TEST_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        "-DARK_TEST_TOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}"
        "-DARK_TEST_NET=$<NOT:$<BOOL:${ASIO_STANDALONE}>>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/check_install.cmake"
    USES_TERMINAL VERBATIM)
# Build selected outputs before staging the install tree.
get_property(ark_runtime_targets GLOBAL PROPERTY ARK_INSTALL_RUNTIME_TARGETS)
if(TARGET app)
    list(APPEND ark_runtime_targets app)
endif()
if(TARGET ark_config)
    list(APPEND ark_runtime_targets ark_config)
endif()
foreach(target IN LISTS ark_package_targets ark_runtime_targets)
    get_target_property(target_type "${target}" TYPE)
    if(NOT target_type STREQUAL "INTERFACE_LIBRARY")
        add_dependencies(check-install "${target}")
    endif()
endforeach()

# CPack archives use only the install rules above. Dependencies remain external.
set(CPACK_PACKAGE_NAME ark)
set(CPACK_PACKAGE_VENDOR OpenArkStudio)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
include(CPack)
