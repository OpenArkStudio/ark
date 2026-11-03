foreach(required PROGRAM PLUGIN_DIR RUNTIME_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${RUNTIME_DIR}/resource/meta" "${RUNTIME_DIR}/conf")
file(WRITE "${RUNTIME_DIR}/resource/meta/entity_schema.json" [=[
{"format":"ark.entity-schema","version":1,"classes":[
{"name":"Player","fields":[{"name":"id","index":99,"type":"uint32"},
{"name":"name","index":100,"type":"string"},{"name":"hp","index":104,"type":"uint32"},
{"name":"flag","index":105,"type":"bool"},{"name":"offset","index":106,"type":"int32"},
{"name":"counter","index":107,"type":"uint64"},{"name":"signed_count","index":108,"type":"int64"},
{"name":"ratio","index":109,"type":"float"},{"name":"precise","index":110,"type":"double"},
{"name":"owner","index":111,"type":"guid"},{"name":"lazy","index":112,"type":"uint32"}],
"tables":[{"name":"items","index":200,"class":"Item"}],
"containers":[{"name":"bag","index":201,"class":"Item"}]},
{"name":"Item","fields":[{"name":"count","index":1,"type":"uint32"},{"name":"label","index":2,"type":"string"}],"tables":[],"containers":[]},
{"name":"LazyOwner","fields":[{"name":"lazy","index":112,"type":"uint32"}],"tables":[],"containers":[]}],
"configs":[{"class":"Player","rows":[{"id":42,"name":"Configured","hp":100,"flag":true,
"offset":-2147483648,"counter":18446744073709551615,"signed_count":-9223372036854775808,
"ratio":1.25,"precise":2.5,"owner":"42","lazy":0}]}]}
]=])
execute_process(COMMAND "${PROGRAM}" "${PLUGIN_DIR}" WORKING_DIRECTORY "${RUNTIME_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Kernel JSON actual DSO regression failed: ${result}\n${output}\n${errors}")
endif()
message(STATUS "Kernel JSON actual DSO regression: ${output}")

# Valid XML sidecars must not provide a fallback when required JSON is absent.
file(WRITE "${RUNTIME_DIR}/resource/meta/config_class.config" "<xml/>")
file(WRITE "${RUNTIME_DIR}/resource/meta/entity_class.config" "<xml><config meta=\"meta/player.config\"/></xml>")
file(WRITE "${RUNTIME_DIR}/resource/meta/player.config" "<xml><meta class=\"Player\" type=\"class\"/></xml>")
foreach(mode missing-json invalid-json)
    if(mode STREQUAL "missing-json")
        file(REMOVE "${RUNTIME_DIR}/resource/meta/entity_schema.json")
    else()
        file(WRITE "${RUNTIME_DIR}/resource/meta/entity_schema.json" "{")
    endif()
    execute_process(COMMAND "${PROGRAM}" "${PLUGIN_DIR}" "${mode}" WORKING_DIRECTORY "${RUNTIME_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "Kernel expected JSON startup rejection")
        message(FATAL_ERROR "Kernel ${mode} startup control failed: ${result}\n${output}\n${errors}")
    endif()
    message(STATUS "${output}")
endforeach()
