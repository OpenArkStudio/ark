# Protobuf messages and compatibility

Protocol definitions live in `src/proto/proto/`. Source files, messages, fields and enum identifiers use `snake_case`. The former `AFMsg` package is now `ark.protobuf`, exposed in C++ as `ark::protobuf`. The `consul` and `osslog` packages retain their names.

## Build and include

Enable `ARK_BUILD_PROTO=ON` and consume `ark::proto` to obtain generated include paths, `ARK_HAS_PROTOBUF` and the Protobuf runtime dependency. Application builds require this option.

```cmake
add_subdirectory(path/to/ark ark-build)
add_executable(my_service main.cpp)
target_link_libraries(my_service PRIVATE ark::core ark::proto)
```

Generated C++ files remain in the CMake build directory under `src/proto/generated/ark/proto/`. Edit the source `.proto` files and let CMake regenerate them; do not edit generated headers. Include a particular message family, for example `<ark/proto/common_messages.pb.h>`, or use the aggregate `<ark/proto.hpp>`. The `.pb.h` suffix belongs to the Protobuf generator and is not a project naming exception to rename manually.

Use Protobuf, Abseil and utf8_range from one compatible installation prefix. In a manual package selection, align `Protobuf_DIR`, `absl_DIR` and `utf8_range_DIR`; a successful link alone does not validate reflection/JSON behavior.

## Message serialization

Messages use the generated Protobuf API directly. The Bus module's message send paths in `src/plugins/bus/msg_module_impl.cpp` call `SerializeToString` and return `false` if serialization fails. The public `msg_module::decode_protobuf` helper in `<ark/bus/msg_module.hpp>` calls `ParseFromString` on the network message body, returning `false` on a parse failure and copying the actor ID on success. See [Bus](bus.md) for the message and transport boundary.

ark does not provide a pluggable message codec API. The generated messages and these Bus helpers are the existing serialization path.

## Migration contract

| Preserved | Requires consumer updates |
| --- | --- |
| Field numbers, types and cardinality | C++ type names and field accessors |
| Enum numeric values and binary payload encoding | Descriptor full names and `Any` type URLs |
| Explicit external JSON field names | Renamed JSON enum strings and code referring to textual type names |

Examples of current names are `ark::protobuf::protobuf_data`, `protobuf_node_data` and `protobuf_entity_data`. API helpers use `protobuf` rather than an abbreviated `pb` prefix; the bus module exposes `msg_module::decode_protobuf`. Rebuild dependent C++ code together with generated messages.

## External JSON spelling

Consul's C++ fields are lower case, while `json_name` explicitly retains its external keys. For example, `consul::service_data::meta()` corresponds to the JSON key `Meta`; `ID`, `Name`, `Address`, `Port`, `Tags` and `Check` retain their required spelling too. Do not replace these external keys just to match C++ identifiers.

The schema contract and representative wire/JSON regressions live in [consul.proto](https://github.com/OpenArkStudio/ark/blob/main/src/proto/proto/consul.proto) and [protobuf_messages.cpp](https://github.com/OpenArkStudio/ark/blob/main/tests/core/protobuf_messages.cpp). Changing textual descriptors is separate from binary compatibility; this migration does not promise complete source or JSON-enum compatibility.
