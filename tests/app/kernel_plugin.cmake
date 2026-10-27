foreach(required PROGRAM PLUGIN_DIR RUNTIME_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${RUNTIME_DIR}/resource/meta" "${RUNTIME_DIR}/conf")
file(REMOVE "${RUNTIME_DIR}/resource/meta/entity_schema.json")
file(WRITE "${RUNTIME_DIR}/resource/meta/config_class.config"
    "<xml><config id=\"attribute\" meta=\"meta/attribute.config\" res=\"meta/attribute.data\"/></xml>")
file(WRITE "${RUNTIME_DIR}/resource/meta/entity_class.config"
    "<xml><config meta=\"meta/player.config\"/></xml>")
file(WRITE "${RUNTIME_DIR}/resource/meta/attribute.config"
    "<xml><meta field=\"id\" type=\"uint64\"/><meta field=\"level\" type=\"int\"/></xml>")
file(WRITE "${RUNTIME_DIR}/resource/meta/attribute.data" "<xml><data id=\"42\" level=\"7\"/></xml>")
file(WRITE "${RUNTIME_DIR}/resource/meta/player.config" "<xml><meta class=\"Player\" type=\"class\"/></xml>")
execute_process(COMMAND "${PROGRAM}" "${PLUGIN_DIR}" WORKING_DIRECTORY "${RUNTIME_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Kernel actual DSO regression failed: ${result}\n${output}\n${errors}")
endif()
message(STATUS "Kernel actual DSO regression: ${output}")
file(WRITE "${RUNTIME_DIR}/resource/meta/entity_schema.json" [=[
{"format":"ark.entity-schema","version":1,"classes":[
{"name":"Player","fields":[{"name":"id","index":99,"type":"uint32"},
{"name":"name","index":100,"type":"string"},{"name":"hp","index":104,"type":"uint32"}],"tables":[],"containers":[]}],
"configs":[{"class":"Player","rows":[{"id":42,"name":"Configured","hp":100}]}]}
]=])
execute_process(COMMAND "${PROGRAM}" "${PLUGIN_DIR}" json WORKING_DIRECTORY "${RUNTIME_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Kernel JSON actual DSO regression failed: ${result}\n${output}\n${errors}")
endif()
message(STATUS "Kernel JSON actual DSO regression: ${output}")
