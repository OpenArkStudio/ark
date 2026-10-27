if(WIN32)
    BUILD_PLUGIN_MACRO(ark_kernel_plugin
        debug AFProto_d.lib
        optimized AFProto.lib
        ${PROTOBUF_LIBRARIES})
elseif(APPLE)
    BUILD_PLUGIN_MACRO(ark_kernel_plugin
        debug ${BIN_OUTPUT_DIR}/AFProto_d.a
        optimized ${BIN_OUTPUT_DIR}/AFProto.a
        ${PROTOBUF_LIBRARIES})
else(UNIX)
    BUILD_PLUGIN_MACRO(ark_kernel_plugin
        debug :AFProto_d.a
        optimized :AFProto.a
        ${PROTOBUF_LIBRARIES})
endif()
set_property(TARGET ark_kernel_plugin APPEND PROPERTY LINK_LIBRARIES nlohmann_json::nlohmann_json)
